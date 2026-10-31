/*
 * ovltest.c -- a still test pattern in the Z3660's hardware overlay, to see
 * how the hardware shows the overlay buffer: bands of 32 rows in 11 colours
 * (red green blue yellow cyan magenta white orange grey purple lime), a
 * white line on each band's first row, written where the MPEG firmware
 * would write a w x h picture at y (row y + j of the buffer, columns 0..).
 *
 *   ovltest [-x x] [-y y] [-w w] [-h h] [-s seconds] [-z]
 *   -z: YSTART back to 0 after the setup (as zzmpeg.c does)
 *
 * The overlay is switched on by the MPEG firmware's INIT, with an empty
 * FIFO (the decoder starts only after 512 KB), and off by CLOSE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

extern int munmap();

#define ZZ9K_IOC            ('Z' << 8)
#define ZZ9KIOC_GETINFO     (ZZ9K_IOC | 1)
#define ZZ9KIOC_SETREG      (ZZ9K_IOC | 7)
struct zz9k_info {
    unsigned long base, board_size, fb_offset, fb_size;
    unsigned short hw_version, fw_version, product, zorro;
    unsigned short width, height, color_mode, mode;
};
struct zz9k_reg { unsigned short offset, value; };
#define FB_PHYS             0x00200000UL

#define REG_MPEG_CMD        0x2C0
#define REG_MPEG_PARAM0     0x2C8
#define REG_OVL_YSTART      0x32C
#define REG_OVL_XSTART      0x330
#define REG_OVL_WIDTH       0x334
#define REG_OVL_HEIGHT      0x338
#define CMD_INIT            1
#define CMD_CLOSE           5
#define OVL_OFF             0x06000000UL
#define FIFO_OFF            0x07A00000UL
#define FIFO_SIZE           0x00100000UL
#define SHARED_OFF          0x07B00000UL

static int fd;

static void reg32(unsigned off, unsigned long v)
{
    struct zz9k_reg r;

    r.offset = off;
    r.value = (unsigned short)(v >> 16);
    ioctl(fd, ZZ9KIOC_SETREG, &r);
    r.offset = off + 2;
    r.value = (unsigned short)v;
    ioctl(fd, ZZ9KIOC_SETREG, &r);
}

static const unsigned long colour[11] = {
    0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00, 0xFF00FFFF, 0xFFFF00FF,
    0xFFFFFFFF, 0xFFFF8000, 0xFF808080, 0xFF8000FF, 0xFF80FF00
};

int main(int argc, char **argv)
{
    struct zz9k_info info;
    unsigned long *ovl;
    volatile unsigned char *sh;
    unsigned char *board;
    int x = 464, y = 116, w = 640, h = 352, secs = 20, zero = 0, i, j, rows;
    size_t len;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-z"))
            zero = 1;
        else if (i + 1 < argc && argv[i][0] == '-') {
            int v = atoi(argv[i + 1]);

            switch (argv[i++][1]) {
            case 'x': x = v; break;
            case 'y': y = v; break;
            case 'w': w = v; break;
            case 'h': h = v; break;
            case 's': secs = v; break;
            }
        }
    }
    fd = open("/dev/zz9000", O_RDWR);
    if (fd < 0 || ioctl(fd, ZZ9KIOC_GETINFO, &info) < 0) {
        perror("/dev/zz9000");
        return 1;
    }
    rows = info.height;
    len = (size_t)info.width * 4 * rows;
    ovl = (unsigned long *)mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)(OVL_OFF - FB_PHYS));
    if (ovl == (unsigned long *)-1) {
        fprintf(stderr, "mmap overlay %lu bytes: %s\n", (unsigned long)len, strerror(errno));
        return 1;
    }
    board = (unsigned char *)mmap(0, (size_t)(SHARED_OFF + 4096 - FIFO_OFF), PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd, (off_t)(FIFO_OFF - FB_PHYS));
    if (board == (unsigned char *)-1) {
        perror("mmap FIFO");
        return 1;
    }
    sh = board + (SHARED_OFF - FIFO_OFF);
    for (i = 0; i < 96; i++)
        sh[i] = 0;
    printf("screen %dx%d; pattern %dx%d at row %d of the buffer, overlay at %d,%d%s\n",
           info.width, info.height, w, h, y, x, y, zero ? ", YSTART then 0" : "");

    /* the whole buffer black-transparent, then the pattern rows */
    for (j = 0; j < rows; j++)
        memset(ovl + (long)j * info.width, 0, (size_t)w * 4);
    for (j = 0; j < h && y + j < rows; j++) {
        unsigned long *p = ovl + (long)(y + j) * info.width;
        unsigned long c = j % 32 == 0 ? 0xFFFFFFFF : colour[(j / 32) % 11];

        for (i = 0; i < w; i++)
            p[i] = i < 8 || i >= w - 8 ? 0xFFFFFFFF : c;    /* white edges */
    }

    reg32(REG_OVL_XSTART, (unsigned long)x);
    reg32(REG_OVL_YSTART, (unsigned long)y);
    reg32(REG_MPEG_PARAM0 + 0, info.base + SHARED_OFF);
    reg32(REG_MPEG_PARAM0 + 4, info.base);
    reg32(REG_MPEG_PARAM0 + 8, FIFO_SIZE);
    reg32(REG_MPEG_PARAM0 + 12, FIFO_OFF);
    reg32(REG_MPEG_CMD, CMD_INIT);
    reg32(REG_OVL_WIDTH, (unsigned long)w);
    reg32(REG_OVL_HEIGHT, (unsigned long)h);
    if (zero)
        reg32(REG_OVL_YSTART, 0);
    sleep(secs);
    reg32(REG_MPEG_CMD, CMD_CLOSE);
    munmap((caddr_t)ovl, len);
    munmap((caddr_t)board, (size_t)(SHARED_OFF + 4096 - FIFO_OFF));
    return 0;
}
