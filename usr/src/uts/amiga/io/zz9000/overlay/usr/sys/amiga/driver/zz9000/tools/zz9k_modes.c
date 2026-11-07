/*
 * zz9k_modes.c - list the board's mode table (ZZ9KIOC_GETMODES).
 *
 *   zz9k_modes [/dev/zz9000]
 *
 * Read-only: opens the device for reading and asks the driver for its
 * table; nothing on the board changes (unlike zz9k_test, which sets a mode).
 * One line per firmware timing, in the driver's order:
 *
 *   1920x1080  60.61 Hz  150.000 MHz  id  5  first current console
 *
 * first: plain SETMODE picks this row for its size; current: the board
 * shows it now; console: the kernel console's mode; novt: too big for the
 * VT console buffer; nofit: bigger than the framebuffer.  Rates and clocks
 * are computed from the firmware's table, not measured ("?" = not known).
 * A kernel without the ioctl answers EINVAL.
 */

#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include "../zz9000.h"

int
main(argc, argv)
int argc;
char **argv;
{
    char *devname;
    int fd;
    unsigned long i;
    struct zz9k_modelist ml;
    static struct zz9k_moderow rows[ZZ9K_MODES_MAX];
    struct zz9k_moderow *r;

    devname = argc > 1 ? argv[1] : "/dev/zz9000";
    if (argc > 2 || devname[0] != '/') {
        printf("usage: zz9k_modes [/dev/zz9000]\n");
        return 2;
    }
    fd = open(devname, O_RDONLY);
    if (fd < 0) {
        perror(devname);
        return 1;
    }
    ml.rows = rows;
    ml.n = ZZ9K_MODES_MAX;
    if (ioctl(fd, ZZ9KIOC_GETMODES, &ml) < 0) {
        if (errno == EINVAL)
            printf("%s: this kernel has no ZZ9KIOC_GETMODES\n", argv[0]);
        else
            perror("ZZ9KIOC_GETMODES");
        close(fd);
        return 1;
    }
    close(fd);
    if (ml.n > ZZ9K_MODES_MAX)
        ml.n = ZZ9K_MODES_MAX;
    for (i = 0; i < ml.n; ++i) {
        r = &rows[i];
        printf("%4ux%-4u ", r->width, r->height);
        if (r->hz100)
            printf("%3u.%02u Hz  ", r->hz100 / 100, r->hz100 % 100);
        else
            printf("     ? Hz  ");
        if (r->pixclk)
            printf("%3lu.%03lu MHz  ", r->pixclk / 1000, r->pixclk % 1000);
        else
            printf("      ? MHz  ");
        printf("id %2u", r->id);
        if (r->flags & ZZ9K_MF_FIRST)
            printf("  first");
        if (r->flags & ZZ9K_MF_CURRENT)
            printf("  current");
        if (r->flags & ZZ9K_MF_CONSOLE)
            printf("  console");
        if (r->flags & ZZ9K_MF_NOVT)
            printf("  novt");
        if (r->flags & ZZ9K_MF_NOFIT)
            printf("  nofit");
        printf("\n");
    }
    return 0;
}
