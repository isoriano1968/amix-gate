/*
 * ovloff.c -- switch the Z3660's hardware overlay off and clear its buffer
 * (left on by a player that died, or showing an old picture).  CLOSE needs
 * the MPEG firmware's shared block, so INIT comes first (with an empty
 * FIFO the decoder does not start).
 */
#include <stdio.h>
#include <string.h>
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
#define REG_OVL_HEIGHT      0x338
#define CMD_INIT            1
#define CMD_CLOSE           5
#define OVL_OFF             0x06000000UL
#define FIFO_OFF            0x07A00000UL
#define FIFO_SIZE           0x00100000UL
#define SHARED_OFF          0x07B00000UL

#define ZZAV_BLK_OFF        0x07AFD000UL        /* arm/h264bench/zzav.h */
#define ZZAV_MAGIC_W        0
#define ZZAV_CMD_W          1
#define ZZAV_STATE_W        2
#define ZZAV_CMD_CLOSE      2

static int fd;

/* core 1's player left running by a program that died: it closes, or the
   overlay comes back with the next frame */
static void player_off(void)
{
    volatile unsigned long *b = (volatile unsigned long *)mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                                                               (off_t)(ZZAV_BLK_OFF - FB_PHYS));
    int i;

    if (b == (volatile unsigned long *)-1)
        return;
    if (b[ZZAV_STATE_W] != 0) {
        b[ZZAV_CMD_W] = ZZAV_CMD_CLOSE;
        for (i = 0; i < 300 && b[ZZAV_CMD_W] == ZZAV_CMD_CLOSE; i++)
            usleep(10000);
        printf("core 1's player: state now %lu, cmd %lu\n", b[ZZAV_STATE_W], b[ZZAV_CMD_W]);
    }
    munmap((caddr_t)b, 4096);
}

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

int main(void)
{
    struct zz9k_info info;
    unsigned char *board, *ovl, *ovl2;
    size_t len;
    int i;

    fd = open("/dev/zz9000", O_RDWR);
    if (fd < 0 || ioctl(fd, ZZ9KIOC_GETINFO, &info) < 0) {
        perror("/dev/zz9000");
        return 1;
    }
    player_off();
    len = (size_t)info.width * 4 * info.height;
    ovl = (unsigned char *)mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)(OVL_OFF - FB_PHYS));
    board = (unsigned char *)mmap(0, (size_t)(SHARED_OFF + 4096 - FIFO_OFF), PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd, (off_t)(FIFO_OFF - FB_PHYS));
    if (ovl == (unsigned char *)-1 || board == (unsigned char *)-1) {
        perror("mmap");
        return 1;
    }
    for (i = 0; i < 96; i++)
        board[SHARED_OFF - FIFO_OFF + i] = 0;
    memset(ovl, 0, len);
    ovl2 = (unsigned char *)mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)(OVL_OFF + 0x800000UL - FB_PHYS));
    if (ovl2 != (unsigned char *)-1) {  /* the second buffer of a double-buffered player */
        memset(ovl2, 0, len);
        munmap((caddr_t)ovl2, len);
    }
    reg32(REG_MPEG_PARAM0 + 0, info.base + SHARED_OFF);
    reg32(REG_MPEG_PARAM0 + 4, info.base);
    reg32(REG_MPEG_PARAM0 + 8, FIFO_SIZE);
    reg32(REG_MPEG_PARAM0 + 12, FIFO_OFF);
    reg32(REG_MPEG_CMD, CMD_INIT);
    reg32(REG_MPEG_CMD, CMD_CLOSE);     /* overlay off */
    munmap((caddr_t)ovl, len);
    munmap((caddr_t)board, (size_t)(SHARED_OFF + 4096 - FIFO_OFF));
    printf("overlay off, buffer cleared\n");
    return 0;
}
