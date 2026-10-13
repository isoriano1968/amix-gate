/*
 * zz9k_monsw.c -- drive the RTG monitor-switch line(s) from Amix userland, with the
 * Z3660 firmware's own `monitor_switch` meanings, for kernels whose zz9000 driver does
 * not do it itself (the r13 lineage).  startx-rtg calls it around the X server.
 *
 * VALUE is the firmware's bit sum (z3660cfg.txt `monitor_switch`, register 0x318):
 *   1  CTS  = CIA-B PRA bit 4 (serial pin 5)      16  CTS level flag
 *   2  SEL  = CIA-B PRA bit 2 (parallel pin 13)   32  SEL level flag
 * With a pin's level flag set, RTG is register bit 0 and native is 1; without it, the
 * reverse -- exactly the AmigaOS P96 driver (z3660-drivers/rtg/gfx.c SetSwitch) and the
 * firmware (main.c monitor_switch).  0 = no switch: the tool does nothing.
 *
 * usage: zz9k_monsw [-n] VALUE rtg|native|off|status
 *   rtg     drive the enabled line(s) to RTG
 *   native  drive them to native
 *   off     native, then hand each line back to the port as an input when its native
 *           level is the pull-up's (1), so the stock serial/parallel drivers find the
 *           power-on state; a line whose native level is 0 stays driven
 *   status  read PRA/DDRA and report, no write
 *   -n      say what would be written, write nothing
 *
 * Access: /dev/mem, lseek + ONE-BYTE read/write of PRA (0xBFD000) and DDRA (0xBFD200),
 * the only /dev/mem path that works on this line.  Every write is a read-modify-write
 * of the enabled bits only; a write that would change any other bit is refused.
 * Order: the PRA level is latched first, then the DDRA bit is set, so a line that
 * turns into an output comes up at the intended level.  Root only.
 *
 * When the zz9000 driver itself drives the switch -- ZZ9KIOC_GETMONSW answers with a
 * value that enables CTS or SEL -- this tool does nothing for rtg/native/off and says so:
 * the kernel owns the line then.  An ENXIO (driver 407d379 and later, value 0) or a 0
 * answer (the earlier RT-03 driver, 03844ac, on KD060-Z) means the driver drives nothing,
 * and the tool drives the line from its own VALUE (MG3, A4000T, 2026-09-28).
 *
 * Build on the box: cc -O -o zz9k_monsw zz9k_monsw.c   (or `make` in this directory)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include "../zz9000.h"

#define PRA     0xBFD000L
#define DDRA    0xBFD200L
#define CTS     0x10
#define SEL     0x04

static int fd = -1, dry = 0;

static int
rd(a)
long a;
{
    unsigned char b;

    if (lseek(fd, a, 0) == -1L) { perror("zz9k_monsw: lseek"); exit(2); }
    if (read(fd, (char *)&b, 1) != 1) { perror("zz9k_monsw: read"); exit(2); }
    return b;
}

static void
wr(a, before, after, mask)
long a;
int before, after, mask;
{
    unsigned char b = after;

    if ((before ^ after) & ~mask & 0xff) {
        fprintf(stderr, "zz9k_monsw: refusing a write to 0x%lx that changes bits outside 0x%02x\n",
                a, mask);
        exit(3);
    }
    if (before == after)
        return;
    if (dry) {
        printf("zz9k_monsw: would write 0x%02x to 0x%lx (was 0x%02x)\n", after & 0xff, a, before);
        return;
    }
    if (lseek(fd, a, 0) == -1L) { perror("zz9k_monsw: lseek"); exit(2); }
    if (write(fd, (char *)&b, 1) != 1) { perror("zz9k_monsw: write"); exit(2); }
}

/* The PRA bit value that means RTG for one pin: 0 with its level flag, else 1. */
static int
rtg_level(value, pin)
int value, pin;
{
    if (pin == CTS)
        return (value & 0x10) ? 0 : 1;
    return (value & 0x20) ? 0 : 1;
}

int
main(argc, argv)
int argc;
char **argv;
{
    int value, pins, pra, ddra, npra, nddra, i, rtg, pin;
    char *cmd, *end;
    long v;

    i = 1;
    if (i < argc && !strcmp(argv[i], "-n")) {
        dry = 1;
        i++;
    }
    if (argc - i != 2) {
        fprintf(stderr, "usage: zz9k_monsw [-n] VALUE rtg|native|off|status\n");
        exit(1);
    }
    v = strtol(argv[i], &end, 0);
    if (*end || v < 0 || (v & ~0x33L)) {
        fprintf(stderr, "zz9k_monsw: VALUE must be a monitor_switch bit sum of 1, 2, 16, 32\n");
        exit(1);
    }
    value = (int) v;
    cmd = argv[i + 1];
    if (strcmp(cmd, "rtg") && strcmp(cmd, "native") && strcmp(cmd, "off") &&
        strcmp(cmd, "status")) {
        fprintf(stderr, "zz9k_monsw: unknown command %s\n", cmd);
        exit(1);
    }

    pins = ((value & 1) ? CTS : 0) | ((value & 2) ? SEL : 0);
    if (!pins && strcmp(cmd, "status"))
        exit(0);                        /* monitor_switch 0: nothing to switch */

    if (strcmp(cmd, "status")) {        /* does the driver own the line? */
        int zfd, kv;

        zfd = open("/dev/zz9000", O_RDONLY);
        if (zfd >= 0) {
            if (ioctl(zfd, ZZ9KIOC_GETMONSW, &kv) == 0 && (kv & 3)) {
                close(zfd);
                printf("zz9k_monsw: the zz9000 driver drives the switch (monitor_switch %d); nothing to do\n",
                       kv);
                exit(0);
            }
            close(zfd);
        }
    }

    fd = open("/dev/mem", (!strcmp(cmd, "status") || dry) ? O_RDONLY : O_RDWR);
    if (fd < 0) {
        perror("zz9k_monsw: /dev/mem");
        exit(2);
    }
    pra = rd(PRA);
    ddra = rd(DDRA);

    if (!strcmp(cmd, "status")) {
        printf("zz9k_monsw: PRA=0x%02x DDRA=0x%02x value=%d\n", pra, ddra, value);
        for (i = 0; i < 2; i++) {
            pin = i ? SEL : CTS;
            if (!(pins & pin))
                continue;
            printf("  %s: %s, level %d = %s\n", pin == SEL ? "SEL" : "CTS",
                   (ddra & pin) ? "driven" : "input",
                   (pra & pin) ? 1 : 0,
                   ((pra & pin) ? 1 : 0) == rtg_level(value, pin) ? "RTG" : "native");
        }
        exit(0);
    }

    rtg = !strcmp(cmd, "rtg");
    npra = pra;
    for (i = 0; i < 2; i++) {
        pin = i ? SEL : CTS;
        if (!(pins & pin))
            continue;
        if ((rtg ? rtg_level(value, pin) : !rtg_level(value, pin)))
            npra |= pin;
        else
            npra &= ~pin;
    }
    wr(PRA, pra, npra, pins);           /* latch the level while still an input */
    nddra = ddra | pins;
    wr(DDRA, ddra, nddra, pins);        /* then drive it */

    if (!strcmp(cmd, "off")) {
        /* hand back a line whose native level is the pull-up's */
        int rel = nddra;

        for (i = 0; i < 2; i++) {
            pin = i ? SEL : CTS;
            if ((pins & pin) && rtg_level(value, pin) == 0)   /* native is 1 */
                rel &= ~pin;
        }
        wr(DDRA, nddra, rel, pins);
        nddra = rel;
    }
    if (!dry)
        printf("zz9k_monsw: %s (PRA 0x%02x->0x%02x, DDRA 0x%02x->0x%02x)\n",
               cmd, pra, npra & 0xff, ddra, nddra & 0xff);
    exit(0);
}
