/*
 * zz9k_mode.c - set or show the RTG mode, drawing nothing.
 *
 *   zz9k_mode [/dev/zz9000]                 show the driver's current mode
 *   zz9k_mode [/dev/zz9000] WxH [16|32]     set the mode (RGB565 or 32-bit)
 *                                           and switch the output to RTG
 *   zz9k_mode [/dev/zz9000] WxH@HZ [16|32]  the same, the timing of that size
 *                                           nearest HZ (e.g. 1920x1080@50,
 *                                           @60.6): ZZ9KIOC_SETMODEHZ
 *
 * The mode names are the resolutions the driver's table for the attached
 * board accepts (EINVAL otherwise); the framebuffer is left as it is.  For
 * a wrapper that chooses the mode before a program that draws by itself.
 */

#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include "../zz9000.h"

static int
parse_res(s, w, h, hz100)
char *s;
unsigned short *w, *h, *hz100;
{
    unsigned int a, b, hz, frac, digits;
    char *p;

    a = 0;
    for (p = s; *p >= '0' && *p <= '9'; ++p)
        a = a * 10 + (*p - '0');
    if (*p != 'x' && *p != 'X')
        return 0;
    b = 0;
    for (++p; *p >= '0' && *p <= '9'; ++p)
        b = b * 10 + (*p - '0');
    *hz100 = 0;
    if (*p == '@') {                    /* HZ, up to two decimals */
        hz = frac = digits = 0;
        for (++p; *p >= '0' && *p <= '9' && hz < 1000; ++p)
            hz = hz * 10 + (*p - '0');
        if (*p == '.')
            for (++p; *p >= '0' && *p <= '9'; ++p)
                if (digits < 2) {
                    frac = frac * 10 + (*p - '0');
                    ++digits;
                }
        if (digits == 1)
            frac *= 10;
        if (hz == 0 || hz > 200)
            return 0;
        *hz100 = (unsigned short)(hz * 100 + frac);
    }
    if (*p != 0 || a == 0 || b == 0 || a > 4096 || b > 4096)
        return 0;
    *w = (unsigned short)a;
    *h = (unsigned short)b;
    return 1;
}

int
main(argc, argv)
int argc;
char **argv;
{
    char *devname;
    int fd, i, rc;
    struct zz9k_info info;
    struct zz9k_mode mode;
    struct zz9k_modehz mh;
    unsigned short hz100;

    devname = "/dev/zz9000";
    i = 1;
    if (argc > i && argv[i][0] == '/')
        devname = argv[i++];
    fd = open(devname, O_RDWR);
    if (fd < 0) {
        perror(devname);
        return 1;
    }
    if (ioctl(fd, ZZ9KIOC_PROBE, 0) < 0) {
        perror("ZZ9KIOC_PROBE");
        close(fd);
        return 1;
    }
    if (argc <= i) {
        if (ioctl(fd, ZZ9KIOC_GETINFO, &info) < 0) {
            perror("ZZ9KIOC_GETINFO");
            close(fd);
            return 1;
        }
        printf("%ux%u %s (mode %u, fb 0x%lx bytes, zorro %u product 0x%02x)\n",
            info.width, info.height,
            info.color_mode == ZZ9K_COLOR_32BIT ? "32" : "16",
            info.mode, info.fb_size, info.zorro, info.product);
        close(fd);
        return 0;
    }
    if (!parse_res(argv[i], &mode.width, &mode.height, &hz100)) {
        printf("usage: zz9k_mode [/dev/zz9000] [WxH[@HZ] [16|32]]\n");
        close(fd);
        return 2;
    }
    mode.color_mode = ZZ9K_COLOR_16BIT565;
    if (argc > i + 1 && argv[i + 1][0] == '3' && argv[i + 1][1] == '2')
        mode.color_mode = ZZ9K_COLOR_32BIT;
    mode.scale_mode = 0;
    if (hz100) {
        mh.width = mode.width;
        mh.height = mode.height;
        mh.hz100 = hz100;
        mh.color_mode = mode.color_mode;
        mh.scale_mode = 0;
        mh.pad = 0;
        rc = ioctl(fd, ZZ9KIOC_SETMODEHZ, &mh);
        if (rc >= 0)
            printf("firmware id %d, %u.%02u Hz\n", rc, mh.hz100 / 100,
                mh.hz100 % 100);
    } else
        rc = ioctl(fd, ZZ9KIOC_SETMODE, &mode);
    if (rc < 0) {
        perror(hz100 ? "ZZ9KIOC_SETMODEHZ" : "ZZ9KIOC_SETMODE");
        printf("%s: mode %ux%u not accepted by the attached board\n", argv[0],
            mode.width, mode.height);
        close(fd);
        return 1;
    }
    rc = ioctl(fd, ZZ9KIOC_SETSWITCH, ZZ9K_SWITCH_RTG);
    if (rc < 0)
        perror("ZZ9KIOC_SETSWITCH RTG");
    printf("%ux%u %s set\n", mode.width, mode.height,
        mode.color_mode == ZZ9K_COLOR_32BIT ? "32" : "16");
    close(fd);
    return 0;
}
