/*
 * zzmpeg.c -- play an MPEG-1 system stream (.mpg: MPEG-1 video + MP2) with
 * the Z3660 firmware's own decoder: the ARM decodes, shows the picture in
 * the RTG's hardware overlay and plays the sound through the board's audio
 * output.  The 68060 only copies the file into a FIFO in board memory.
 *
 *   zzmpeg [-x x] [-y y] file.mpg
 *
 * The firmware side (shanshe/Z3660, v1.03 beta 21: mpg/z3660_mpeg_handler.c,
 * mpg/pl_mpeg_arm/pl_mpeg_handler.c, rtg/rtg.c):
 *   - REG_ZZ_MPEG_PARAM0..4 (0x2C8..0x2D8): parameters, then a command in
 *     REG_ZZ_MPEG_CMD_OP (0x2C0): INIT 1 (PARAM0 the 68k address of a
 *     shared block, PARAM1 the board's 68k address, PARAM2 the FIFO size,
 *     PARAM3 its offset in board memory), PROCESS 2 (take what the FIFO has
 *     up to the write index), RESET 3, CLOSE 5;
 *   - REG_ZZ_MPEG_FIFOTX (0x2E4): the FIFO's write index;
 *   - the shared block (arm_decoder_shared_arm_t): big-endian values laid
 *     out by the ARM's compiler (doubles 8-aligned), read here by offset.
 * The first command must be INIT: every command writes its status through
 * the block's pointer, which is NULL until INIT.  The decoder starts once
 * 512 KB have arrived; its input buffer grows without bound, so the file is
 * fed at the stream's own rate (the pack header's mux_rate) ahead of the
 * video time the ARM reports, not as fast as the 68k can copy.
 *
 * Needs the zz9000 driver with ZZ9K_MAP_BOARD (board memory by mmap) and
 * GETREG/SETREG (32-bit registers as two 16-bit halves, high first).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>

/* the AMIX zz9000 driver (zz9000.h) */
#define ZZ9K_IOC            ('Z' << 8)
#define ZZ9KIOC_GETINFO     (ZZ9K_IOC | 1)
#define ZZ9KIOC_GETREG      (ZZ9K_IOC | 6)
#define ZZ9KIOC_SETREG      (ZZ9K_IOC | 7)
struct zz9k_info {
    unsigned long base, board_size, fb_offset, fb_size;
    unsigned short hw_version, fw_version, product, zorro;
    unsigned short width, height, color_mode, mode;
};
struct zz9k_reg { unsigned short offset, value; };
#define FB_PHYS             0x00200000UL    /* device offset 0 = board + 2 MB */

/* the firmware */
#define REG_MPEG_CMD        0x2C0
#define REG_MPEG_PARAM0     0x2C8
#define REG_MPEG_FIFOTX     0x2E4
#define REG_OVL_YSTART      0x32C
#define REG_OVL_XSTART      0x330
#define REG_OVL_WIDTH       0x334
#define REG_OVL_HEIGHT      0x338
#define CMD_INIT            1
#define CMD_PROCESS         2
#define CMD_CLOSE           5
#define ST_VIDEO_INIT       0x10
#define ST_FINISHED         0x20
/* the shared block, by the ARM's offsets */
#define SH_STATUS           0
#define SH_STATUS2          4
#define SH_FRAMES           8
#define SH_SYNC_TIME        24
#define SH_WIDTH            32
#define SH_HEIGHT           36
#define SH_FRAMERATE        40      /* float */
#define SH_MB_LEFT          56
#define SH_BYTES_LEFT       60
#define SH_SIZE             96

/* where in board memory (offsets from the board's start, as the ARM sees
   RTG_BASE): clear of soft3D (textures 0x0E00000-0x25FFFFF, its data and
   heap 0x2600000-0x52FFFFF), the overlay (0x6000000, 8 MB at 1920x1080x4)
   and the firmware's audio buffers (0x7000000, 0x7DE0000) */
#define FIFO_OFF            0x07A00000UL
#define FIFO_SIZE           0x00100000UL
#define SHARED_OFF          0x07B00000UL
#define CHUNK               (64 * 1024)     /* < 512 KB: the firmware's own ring is 1 MB */
#define AHEAD               (1536 * 1024L)  /* bytes fed ahead of the picture */

static int fd = -1, inited;

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

static unsigned long be32(const volatile unsigned char *p)
{
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

/* a double the ARM may be writing while it is read: the same twice */
static double be64d(const volatile unsigned char *p)
{
    union { unsigned char b[8]; double d; } u, v;
    int i, tries = 0;

    do {
        for (i = 0; i < 8; i++)
            u.b[i] = p[i];      /* both sides big-endian IEEE */
        for (i = 0; i < 8; i++)
            v.b[i] = p[i];
    } while ((memcmp(u.b, v.b, 8) || u.d != u.d) && ++tries < 10);
    return u.d == u.d ? u.d : 0.0;
}

static double now(void)
{
    struct timeval tv;

    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static void stop(int sig)
{
    if (inited)
        reg32(REG_MPEG_CMD, CMD_CLOSE);     /* overlay off, decoder freed */
    if (sig) {
        signal(sig, SIG_DFL);
        kill(getpid(), sig);
    }
}

/* the stream's rate in bytes a second, from the first MPEG-1 pack header */
static long mux_rate(const unsigned char *p, long n)
{
    long i;

    for (i = 0; i + 12 <= n; i++)
        if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1 && p[i + 3] == 0xBA) {
            const unsigned char *h = p + i + 4;

            if ((h[0] & 0xF0) != 0x20)
                return -1;              /* MPEG-2 pack: not for pl_mpeg */
            return (((long)(h[5] & 0x7F) << 15) | ((long)h[6] << 7) | (h[7] >> 1)) * 50;
        }
    return 0;
}

int main(int argc, char **argv)
{
    struct zz9k_info info;
    unsigned char *board, *fifo, *buf;
    volatile unsigned char *sh;
    int ov_x = 64, ov_y = 64, i, f, w = 0, h = 0;
    long rate, n, sent = 0;
    unsigned long wr = 0, fr = 0;
    double t0, last_vt = -1, last_change;
    const char *file = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-x") && i + 1 < argc)
            ov_x = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-y") && i + 1 < argc)
            ov_y = atoi(argv[++i]);
        else
            file = argv[i];
    }
    if (!file) {
        fprintf(stderr, "usage: zzmpeg [-x x] [-y y] file.mpg\n");
        return 2;
    }
    f = open(file, O_RDONLY);
    if (f < 0) {
        perror(file);
        return 1;
    }
    buf = (unsigned char *)malloc(CHUNK);
    n = read(f, buf, CHUNK);
    rate = mux_rate(buf, n);
    if (rate <= 0) {
        fprintf(stderr, "%s: no MPEG-1 system stream (pack header)\n", file);
        return 1;
    }
    lseek(f, 0L, SEEK_SET);
    fd = open("/dev/zz9000", O_RDWR);
    if (fd < 0 || ioctl(fd, ZZ9KIOC_GETINFO, &info) < 0) {
        perror("/dev/zz9000");
        return 1;
    }
    /* info.board_size is the framebuffer's part only; with ZZ9K_MAP_BOARD
       mmap() reaches to the board's end, and refuses what lies beyond */
    board = (unsigned char *)mmap(0, (size_t)(SHARED_OFF + 4096 - FIFO_OFF), PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd, (off_t)(FIFO_OFF - FB_PHYS));
    if (board == (unsigned char *)-1) {
        perror("mmap (needs a ZZ9K_MAP_BOARD kernel)");
        return 1;
    }
    fifo = board;
    sh = board + (SHARED_OFF - FIFO_OFF);
    for (i = 0; i < SH_SIZE; i++)
        sh[i] = 0;
    printf("%s: %ld bytes a second; board at %08lx, FIFO at +%08lx\n", file, rate, info.base, FIFO_OFF);

    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    signal(SIGHUP, stop);
    reg32(REG_OVL_XSTART, (unsigned long)ov_x);
    reg32(REG_OVL_YSTART, (unsigned long)ov_y);
    reg32(REG_MPEG_PARAM0 + 0, info.base + SHARED_OFF);
    reg32(REG_MPEG_PARAM0 + 4, info.base);
    reg32(REG_MPEG_PARAM0 + 8, FIFO_SIZE);
    reg32(REG_MPEG_PARAM0 + 12, FIFO_OFF);
    reg32(REG_MPEG_CMD, CMD_INIT);
    inited = 1;

    t0 = last_change = now();
    for (;;) {
        double vt = be64d(sh + SH_SYNC_TIME);
        unsigned long st2 = be32(sh + SH_STATUS2);
        long allowed;

        if (!w && (st2 & ST_VIDEO_INIT) && be32(sh + SH_WIDTH)) {
            w = (int)be32(sh + SH_WIDTH);
            h = (int)be32(sh + SH_HEIGHT);
            reg32(REG_OVL_WIDTH, (unsigned long)w);
            reg32(REG_OVL_HEIGHT, (unsigned long)h);     /* re-enables the overlay */
            printf("%dx%d after %.1f s\n", w, h, now() - t0);
        }
        if (vt != last_vt) {
            last_vt = vt;
            last_change = now();
            if (++fr % 50 == 0)
                printf("video %.1f s, fed %ld KB, %.1f s wall\n", vt, sent >> 10, now() - t0);
        }
        /* feed until AHEAD past what the picture has used */
        allowed = AHEAD + (long)((vt > 0 ? vt : 0) * rate);
        if (f >= 0 && sent < allowed) {
            n = read(f, buf, CHUNK);
            if (n <= 0) {
                close(f);
                f = -1;
                printf("file sent (%ld KB) after %.1f s\n", sent >> 10, now() - t0);
            } else {
                long k = n, first = (long)(FIFO_SIZE - wr);

                if (k > first) {
                    memcpy(fifo + wr, buf, (size_t)first);
                    memcpy(fifo, buf + first, (size_t)(k - first));
                } else
                    memcpy(fifo + wr, buf, (size_t)k);
                wr = (wr + (unsigned long)k) % FIFO_SIZE;
                sent += k;
                reg32(REG_MPEG_FIFOTX, wr);
                reg32(REG_MPEG_CMD, CMD_PROCESS);
                continue;
            }
        }
        if ((st2 & ST_FINISHED) || (f < 0 && vt > 0 && now() - last_change > 3.0))
            break;
        if (vt <= 0 && f < 0 && now() - t0 > 20.0) {
            fprintf(stderr, "no picture after 20 s (status %08lx/%08lx)\n", be32(sh + SH_STATUS), st2);
            break;
        }
        usleep(20000);
    }
    printf("end: video %.1f s, %.1f s wall\n", last_vt, now() - t0);
    stop(0);
    return 0;
}
