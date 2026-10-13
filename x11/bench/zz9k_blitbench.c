/*
** zz9k_blitbench.c -- where does a blitter record's time go?  (M1 of the per-band analysis,
** NOTES "Where the per-band time goes", 2026-09-29; 1.1 preparation.)
**
** Talks to /dev/zz9000 directly, no X server: sets 1024x768 16-bit, then times batches of
** ZZ9KIOC_BLITS records (the driver's register cache lives for one ioctl, so identical records
** after the first cost only the trigger write) and prints microseconds per record:
**
**   a  1x1 fill, identical records, 1024 per ioctl   -> the fixed cost of one card operation
**   b  1x1 fill, y alternating 0/1                    -> (b - a) = one register write
**   c  8192-px fill (1024x8), identical               -> (c - a) / 8191 = cost per pixel
**   d  1x1 fill, one record per ioctl                 -> (d - a) = one ioctl
**   h  1x1 fill, x, y, width and height all changing   -> (h - a) / 4 = one register write,
**                                                        a second estimate beside b
**   g  gettimeofday itself                            -> the clock's own cost (a check)
**   e  1x1 copy, identical                            -> a copy's fixed cost
**   f  8192-px copy (1024x8, one row down), identical -> (f - e) / 8191 = copy cost per pixel
**
** Each case repeats whole batches until at least -secs seconds have passed (default 2;
** gettimeofday may tick at 10 ms).  Draws in the top rows of the screen only.
**
** Usage:   zz9k_blitbench [-secs N] [-dev /dev/zz9000]
**          Run with no X server on the card (it sets the mode itself); as root.
** Exit:    0 measured, 2 could not run (no device, no mode, no blitter: ENXIO).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include "zz9000.h"

#define BATCH 1024

static int fd;
static struct zz9k_blit recs[BATCH];

static double
now()
{
    struct timeval tv;

    gettimeofday(&tv, (struct timezone *) 0);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static void
rec(r, op, x, y, w, h, sx, sy, color)
struct zz9k_blit *r;
int op, x, y, w, h, sx, sy;
unsigned long color;
{
    r->op = op;
    r->x = x;
    r->y = y;
    r->width = w;
    r->height = h;
    r->src_x = sx;
    r->src_y = sy;
    r->pad = 0;
    r->color = color;
}

/*
** Run n-record batches until secs have passed; return microseconds per record.  The clock is
** read once per 1024 records whatever n is: the first build read it after every ioctl, so its
** case d (one record per ioctl) timed an ioctl plus a gettimeofday (M1 on metal, 2026-09-29).
*/
static double
run(n, secs, label)
int n;
double secs;
char *label;
{
    struct zz9k_blits bs;
    double t0, t;
    long records = 0;
    int k, per = BATCH / n;

    bs.recs = recs;
    bs.n = n;
    t0 = now();
    do {
        for (k = 0; k < per; k++)
            if (ioctl(fd, ZZ9KIOC_BLITS, &bs) < 0) {
                fprintf(stderr, "zz9k_blitbench: %s: ZZ9KIOC_BLITS: %s\n", label, strerror(errno));
                exit(2);
            }
        records += (long) n * per;
        t = now();
    } while (t - t0 < secs);
    return (t - t0) * 1e6 / records;
}

/* Microseconds per gettimeofday call. */
static double
clockcost(secs)
double secs;
{
    double t0, t;
    long calls = 0;

    t0 = now();
    do {
        t = now();
        calls++;
    } while (t - t0 < secs);
    return (t - t0) * 1e6 / calls;
}

int
main(argc, argv)
int argc;
char *argv[];
{
    struct zz9k_mode m;
    char *dev = "/dev/zz9000";
    double secs = 2.0, a, b, c, d, e, f, g, h;
    int i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-secs") && i + 1 < argc)
            secs = atof(argv[++i]);
        else if (!strcmp(argv[i], "-dev") && i + 1 < argc)
            dev = argv[++i];
        else {
            fprintf(stderr, "usage: zz9k_blitbench [-secs N] [-dev /dev/zz9000]\n");
            return 2;
        }
    }
    fd = open(dev, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "zz9k_blitbench: %s: %s\n", dev, strerror(errno));
        return 2;
    }
    m.width = 1024;
    m.height = 768;
    m.color_mode = ZZ9K_COLOR_16BIT565;
    m.scale_mode = 0;
    if (ioctl(fd, ZZ9KIOC_SETMODE, &m) < 0) {
        fprintf(stderr, "zz9k_blitbench: SETMODE 1024x768/16: %s\n", strerror(errno));
        return 2;
    }

    for (i = 0; i < BATCH; i++)
        rec(&recs[i], ZZ9K_BLIT_FILL, 0, 0, 1, 1, 0, 0, 0xf800);
    a = run(BATCH, secs, "a");
    for (i = 0; i < BATCH; i++)
        rec(&recs[i], ZZ9K_BLIT_FILL, 0, i & 1, 1, 1, 0, 0, 0x07e0);
    b = run(BATCH, secs, "b");
    for (i = 0; i < BATCH; i++)
        rec(&recs[i], ZZ9K_BLIT_FILL, 0, 0, 1024, 8, 0, 0, 0x001f);
    c = run(BATCH, secs, "c");
    rec(&recs[0], ZZ9K_BLIT_FILL, 0, 0, 1, 1, 0, 0, 0xffff);
    d = run(1, secs, "d");
    for (i = 0; i < BATCH; i++)
        rec(&recs[i], ZZ9K_BLIT_COPY, 1, 0, 1, 1, 0, 0, 0);
    e = run(BATCH, secs, "e");
    for (i = 0; i < BATCH; i++)
        rec(&recs[i], ZZ9K_BLIT_COPY, 0, 1, 1024, 8, 0, 0, 0);
    f = run(BATCH, secs, "f");
    for (i = 0; i < BATCH; i++)         /* X1, Y1, X2, Y2 differ from the previous record */
        rec(&recs[i], ZZ9K_BLIT_FILL, i & 1, i & 1, 1 + (i & 1), 1 + (i & 1), 0, 0, 0xf81f);
    h = run(BATCH, secs, "h");
    g = clockcost(secs);

    printf("zz9k_blitbench: microseconds per record (%.1f s per case)\n", secs);
    printf("  a  fill 1x1 identical, batched     %9.2f   fixed cost of one card op\n", a);
    printf("  b  fill 1x1 y alternating          %9.2f   b-a = %.2f per register write\n", b, b - a);
    printf("  c  fill 1024x8 identical           %9.2f   (c-a)/8191 = %.4f per pixel\n", c, (c - a) / 8191);
    printf("  d  fill 1x1, one record per ioctl  %9.2f   d-a = %.2f per ioctl\n", d, d - a);
    printf("  e  copy 1x1 identical              %9.2f\n", e);
    printf("  f  copy 1024x8 identical           %9.2f   (f-e)/8191 = %.4f per pixel\n", f, (f - e) / 8191);
    printf("  h  fill x/y/w/h all changing       %9.2f   (h-a)/4 = %.2f per register write\n", h, (h - a) / 4);
    printf("  g  gettimeofday                    %9.2f   per call (not part of a-h)\n", g);
    fflush(stdout);
    close(fd);
    return 0;
}
