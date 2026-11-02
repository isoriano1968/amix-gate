/*
 * zzglstat.c -- zzgl's block once a second (AMIX): frames shown, the ring's
 * write and read positions, errors, the sound position.
 *   zzglstat [seconds]
 *   zzglstat -close        (ends a server whose program died: a ZZ_CLOSE into the ring,
 *                           and the overlay it left on goes off, as ovloff does it)
 *   zzglstat -grab FILE    (the overlay buffer shown now: P6 PPM)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "zzgl.h"

extern int munmap();

#define FB_PHYS 0x00200000UL
#define PAGE    4096UL

/* the overlay off: ovloff.c's register sequence (CLOSE needs INIT first) */
#define ZZ9KIOC_GETINFO     (('Z' << 8) | 1)
#define ZZ9KIOC_SETREG      (('Z' << 8) | 7)
#define REG_MPEG_CMD        0x2C0
#define REG_MPEG_PARAM0     0x2C8
#define CMD_INIT            1
#define CMD_CLOSE           5
#define FIFO_OFF            0x07A00000UL
#define FIFO_SIZE           0x00100000UL
#define SHARED_OFF          0x07B00000UL
struct zz9k_info {
    unsigned long base, board_size, fb_offset, fb_size;
    unsigned short hw_version, fw_version, product, zorro;
    unsigned short width, height, color_mode, mode;
};
struct zz9k_reg { unsigned short offset, value; };

static void reg32(int fd, unsigned off, unsigned long v)
{
    struct zz9k_reg r;

    r.offset = off;
    r.value = (unsigned short)(v >> 16);
    ioctl(fd, ZZ9KIOC_SETREG, &r);
    r.offset = off + 2;
    r.value = (unsigned short)v;
    ioctl(fd, ZZ9KIOC_SETREG, &r);
}

static void overlay_off(int fd)
{
    struct zz9k_info info;
    unsigned char *shared;
    int i;

    if (ioctl(fd, ZZ9KIOC_GETINFO, &info) < 0)
        return;
    shared = (unsigned char *)mmap(0, PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)(SHARED_OFF - FB_PHYS));
    if (shared == (unsigned char *)-1)
        return;
    for (i = 0; i < 96; i++)
        shared[i] = 0;
    reg32(fd, REG_MPEG_PARAM0 + 0, info.base + SHARED_OFF);
    reg32(fd, REG_MPEG_PARAM0 + 4, info.base);
    reg32(fd, REG_MPEG_PARAM0 + 8, FIFO_SIZE);
    reg32(fd, REG_MPEG_PARAM0 + 12, FIFO_OFF);
    reg32(fd, REG_MPEG_CMD, CMD_INIT);
    reg32(fd, REG_MPEG_CMD, CMD_CLOSE);
    munmap((caddr_t)shared, PAGE);
}

int main(int argc, char **argv)
{
    int fd = open("/dev/zz9000", O_RDWR), n = argc > 1 ? atoi(argv[1]) : 10, i;
    volatile unsigned long *b;
    unsigned long f0;

    if (fd < 0 || (b = (volatile unsigned long *)mmap(0, PAGE, PROT_READ, MAP_SHARED, fd,
                                                      (off_t)(ZZGL_BLK_OFF - FB_PHYS))) == (void *)-1) {
        perror("zzglstat");
        return 1;
    }
    if (argc > 1 && !strcmp(argv[1], "-close")) {
        volatile unsigned long *w = (volatile unsigned long *)mmap(0, PAGE, PROT_READ | PROT_WRITE,
                                                                   MAP_SHARED, fd, (off_t)(ZZGL_BLK_OFF - FB_PHYS));
        volatile unsigned long *ring = (volatile unsigned long *)mmap(0, ZZGL_RING_SIZE, PROT_READ | PROT_WRITE,
                                                                      MAP_SHARED, fd, (off_t)(ZZGL_RING_OFF - FB_PHYS));
        unsigned long wr;

        if (w == (void *)-1 || ring == (void *)-1 || w[ZB_MAGIC] != ZZGL_MAGIC) {
            fprintf(stderr, "zzglstat: no server\n");
            return 1;
        }
        w[ZB_OVL_ROW] = ZB_OVL_OFF;     /* a last frame must not bring it back */
        wr = w[ZB_WR];
        if (wr + 16 > ZZGL_RING_SIZE) {
            if (wr + 8 <= ZZGL_RING_SIZE)
                ring[wr / 4] = ZZ_WRAP, ring[wr / 4 + 1] = 0;
            wr = 0;
        }
        ring[wr / 4] = ZZ_CLOSE;
        ring[wr / 4 + 1] = 0;
        w[ZB_WR] = wr + 8;
        sleep(1);
        if (w[ZB_MAGIC] != ZZGL_MAGIC)
            overlay_off(fd);
        printf("zzglstat: server %s\n", w[ZB_MAGIC] == ZZGL_MAGIC ? "still runs" : "ended");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "-grab")) {
        const unsigned long pitch = b[ZB_OVL], row0 = b[ZB_OVL + 1], w = b[ZB_OVL + 2], h = b[ZB_OVL + 3];
        volatile unsigned long *c1ld = (volatile unsigned long *)mmap(0, PAGE, PROT_READ, MAP_SHARED, fd,
                                                                      (off_t)(0x07AFE000UL - FB_PHYS));
        unsigned long buf, base, len, x, y;
        unsigned char *p;
        FILE *o;

        if (!pitch || !w || !h || c1ld == (void *)-1) {
            fprintf(stderr, "zzglstat: no overlay\n");
            return 1;
        }
        buf = c1ld[10] == 2 ? 0x06800000UL : 0x06000000UL;     /* C1LD_W_FRONT */
        base = (buf + row0 * pitch) & ~(PAGE - 1);
        len = (buf + row0 * pitch - base + h * pitch + PAGE - 1) & ~(PAGE - 1);
        p = (unsigned char *)mmap(0, len, PROT_READ, MAP_SHARED, fd, (off_t)(base - FB_PHYS));
        if (p == (void *)-1 || !(o = fopen(argv[2], "wb"))) {
            perror("zzglstat -grab");
            return 1;
        }
        p += buf + row0 * pitch - base;
        fprintf(o, "P6\n%lu %lu\n255\n", w, h);
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
            {
                /* 0x00RRGGBB as core 1 (little-endian) stores it: B, G, R, 0 */
                const unsigned char *q = p + y * pitch + x * 4;
                const unsigned char rgb[3] = { q[2], q[1], q[0] };

                fwrite(rgb, 1, 3, o);
            }
        fclose(o);
        printf("zzglstat: %lux%lu into %s\n", w, h, argv[2]);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "-ring")) {
        /* the commands not yet taken: what core 1 is at, for a hang */
        const unsigned char *r = (const unsigned char *)mmap(0, ZZGL_RING_SIZE, PROT_READ, MAP_SHARED, fd,
                                                             (off_t)(ZZGL_RING_OFF - FB_PHYS));
        unsigned long rd = b[ZB_RD], wr = b[ZB_WR], k;

        if (r == (void *)-1) {
            perror("zzglstat -ring");
            return 1;
        }
        printf("rd %lu wr %lu; command %lu\n", rd, wr, b[ZB_MARK]);
        printf("sample %lu: pc %08lx lr %08lx sp %08lx stack", b[ZB_SAMPLE], b[ZB_SAMPLE + 1], b[ZB_SAMPLE + 2],
               b[ZB_SAMPLE + 3]);
        for (k = ZB_SAMPLE + 4; k < ZB_REPLY && b[k]; k++)
            printf(" %08lx", b[k]);
        printf("\n");
        for (i = 0; i < 40 && rd != wr; i++) {
            const unsigned long *w = (const unsigned long *)(r + rd);

            if (w[0] == ZZ_WRAP) {
                printf("%8lu wrap\n", rd);
                rd = 0;
                continue;
            }
            printf("%8lu op %lu n %lu:", rd, w[0], w[1]);
            for (k = 0; k < (i ? 8 : 512) && k * 4 < w[1]; k++)
                printf(" %08lx", w[2 + k]);
            printf("\n");
            rd = (rd + ((8 + w[1] + 7) & ~7UL)) % ZZGL_RING_SIZE;
        }
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "-hdmi")) {
        /* what the HDMI plays and the stream, as they are now, into a file */
        const char *hd = (const char *)mmap(0, 0x8000, PROT_READ, MAP_SHARED, fd, (off_t)(ZZGL_HDMI_OFF - FB_PHYS));
        const char *st = (const char *)mmap(0, ZZGL_SND_SIZE, PROT_READ, MAP_SHARED, fd, (off_t)(ZZGL_SND_OFF - FB_PHYS));
        FILE *o = fopen(argv[2], "wb");
        unsigned long pos = b[ZB_SND_POS], wr = b[ZB_SND_WR];

        if (!o || hd == (void *)-1 || st == (void *)-1) {
            perror("zzglstat -hdmi");
            return 1;
        }
        fwrite(&pos, 4, 1, o);
        fwrite(&wr, 4, 1, o);
        fwrite(hd, 1, ZZGL_HDMI_SIZE, o);
        fwrite(st, 1, ZZGL_SND_SIZE, o);
        fclose(o);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "-snd")) {
        /* the sound: positions, and how much of the stream and of the HDMI
           ring is not silence */
        const unsigned short *st = (const unsigned short *)mmap(0, ZZGL_SND_SIZE, PROT_READ, MAP_SHARED, fd,
                                                                 (off_t)(ZZGL_SND_OFF - FB_PHYS));
        const unsigned short *hd = (const unsigned short *)mmap(0, 0x8000, PROT_READ, MAP_SHARED, fd,
                                                                (off_t)(ZZGL_HDMI_OFF - FB_PHYS));
        unsigned long k, nz_st = 0, nz_hd = 0, max_hd = 0;

        if (st == (void *)-1 || hd == (void *)-1) {
            perror("zzglstat -snd");
            return 1;
        }
        for (i = 0; i < 3; i++) {
            nz_st = nz_hd = max_hd = 0;
            for (k = 0; k < ZZGL_SND_SIZE / 2; k++)
                nz_st += st[k] != 0;
            for (k = 0; k < ZZGL_HDMI_SIZE / 2; k++) {
                const unsigned v = hd[k];
                const int sv = (short)((v >> 8) | (v << 8));    /* little-endian */

                nz_hd += v != 0;
                if ((unsigned long)(sv < 0 ? -sv : sv) > max_hd)
                    max_hd = (unsigned long)(sv < 0 ? -sv : sv);
            }
            printf("snd on %lu wr %lu pos %lu under %lu; stream %lu of %lu not 0, HDMI ring %lu of %lu not 0 (peak %lu)\n",
                   b[ZB_SND_ON], b[ZB_SND_WR], b[ZB_SND_POS], b[ZB_SND_UNDER],
                   nz_st, ZZGL_SND_SIZE / 2, nz_hd, ZZGL_HDMI_SIZE / 2, max_hd);
            sleep(1);
        }
        return 0;
    }
    f0 = b[ZB_FRAMES];
    for (i = 0; i < n; i++) {
        sleep(1);
        printf("magic %08lx frames %lu (+%lu) wr %lu rd %lu sync %lu error %08lx audio %lu/%lu\n",
               b[ZB_MAGIC], b[ZB_FRAMES], b[ZB_FRAMES] - f0, b[ZB_WR], b[ZB_RD], b[ZB_SYNC_ACK],
               b[ZB_ERROR], b[ZB_AUDIO_POS], b[ZB_AUDIO_SEQ]);
        printf("  core 1: frame %lu ms; draw %lu, fast tris %lu (spans %lu), prepare %lu, "
               "slow tris %lu: %lu (k cycles); %lu spans %lu pixels, %lu L1 refills\n",
               b[ZB_STATS_US] / 1000, b[ZB_PROF + 3], b[ZB_PROF + 0], b[ZB_PROF + 1], b[ZB_PROF + 2],
               b[ZB_PROF + 6], b[ZB_PROF + 7], b[ZB_PROF + 4], b[ZB_PROF + 5], b[ZB_PROF_EVT]);
        printf("  pixels: affine NEON %lu, perspective NEON %lu, scalar %lu; perspective setups %lu: %lu k cycles\n",
               b[ZB_PROF_PATH], b[ZB_PROF_PATH + 1], b[ZB_PROF_PATH2], b[ZB_PROF_SEG + 1], b[ZB_PROF_SEG]);
        printf("  routes: %lu pixels two a turn, %lu of them direct\n", b[ZB_PROF_ROUTE], b[ZB_PROF_ROUTE + 1]);
        f0 = b[ZB_FRAMES];
    }
    return 0;
}
