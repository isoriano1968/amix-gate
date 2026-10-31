/* zzpanfix.c -- BENCH DIAGNOSTIC, not part of the driver.  Re-issue the pan with the
 * blitter X2 register holding the mode width, through the driver's own SETREG ioctl.
 * amiberry's ZZ9000 emulation takes the display pitch from X2 at every PAN_LO write
 * (pan_width = X2); the ZZ9000-derived firmware treats X2 == 0 as "mode width".  The
 * driver writes X2 = 0 before PAN = 0 on SWITCH_RTG, which is correct for the firmware
 * and renders row 0 forever under amiberry.  Usage: zzpanfix /dev/zz9000 <width> */
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include "zz9000.h"
main(argc, argv)
int argc; char **argv;
{
    struct zz9k_reg r; int fd, w;
    if (argc < 3) { printf("usage: zzpanfix /dev/zz9000 <width>\n"); return 2; }
    w = atoi(argv[2]);
    fd = open(argv[1], O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    r.offset = ZZ9K_REG_BLITTER_X2; r.value = w;
    if (ioctl(fd, ZZ9KIOC_SETREG, &r) < 0) { perror("SETREG X2"); return 1; }
    r.offset = ZZ9K_REG_PAN_HI; r.value = 0;
    if (ioctl(fd, ZZ9KIOC_SETREG, &r) < 0) { perror("SETREG PAN_HI"); return 1; }
    r.offset = ZZ9K_REG_PAN_LO; r.value = 0;
    if (ioctl(fd, ZZ9KIOC_SETREG, &r) < 0) { perror("SETREG PAN_LO"); return 1; }
    printf("zzpanfix: X2=%d, PAN=0 re-issued\n", w);
    close(fd);
    return 0;
}
