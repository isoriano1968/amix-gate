/*
 * glshow.c -- glbench's frames on the screen (AMIX, 68060 side).
 *
 *   glshow [gears|q3|q3mt|fluid] [frames] [x y]
 *   glshow -grab FILE      (the frame a job with flag 32 left: RGBA, bottom row first)
 *
 * Core 1 must run z3660ARM_Core_glbench.bin.  The RTG overlay is switched
 * on as vplay does it (zzav.c: the firmware's MPEG INIT, then place and
 * size), a glbench job with flag 16 is posted (320x240), and core 1 puts
 * each frame into the overlay; the overlay goes off when the job is done.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include "bench.h"
#include "zzav.h"

extern int munmap();                    /* not in AMIX's sys/mman.h */

/* the AMIX zz9000 driver (zz9000.h), as zzav.c */
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
#define FIFO_OFF            0x07A00000UL
#define FIFO_SIZE           0x00010000UL
#define SHARED_OFF          0x07B00000UL
#define PAGE                4096UL

#define GLB_SHOW            16          /* glbench.c: frames onto the overlay */
#define W                   320
#define H                   240

static int fd = -1;

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

static void *map(unsigned long off, unsigned long len)
{
    void *p = mmap(0, (size_t)len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)(off - FB_PHYS));

    return p == (void *)-1 ? NULL : p;
}

static void place(int x, int y, int vs)
{
    reg32(REG_OVL_XSTART, (unsigned long)x);
    reg32(REG_OVL_YSTART, (unsigned long)y);
    reg32(REG_OVL_WIDTH, W);
    reg32(REG_OVL_HEIGHT, (unsigned long)(H * vs));
}

static double now(void)
{
    struct timeval tv;

    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

/* the frame glbench left with flag 32 into FILE */
static int grab(const char *file)
{
    volatile unsigned long *ctl;
    unsigned char *img;
    unsigned long size;
    FILE *o;

    if ((fd = open("/dev/zz9000", O_RDWR)) < 0 || !(ctl = (volatile unsigned long *)map(HB_CTL_OFF, PAGE))) {
        perror("/dev/zz9000");
        return 1;
    }
    size = ctl[W_WIDTH] * ctl[W_HEIGHT] * 4;
    if (!size || size > 0x600000 ||
        !(img = (unsigned char *)map(HB_DATA_OFF + 0x100000UL, (size + PAGE - 1) & ~(PAGE - 1)))) {
        fprintf(stderr, "glshow: no frame (%lux%lu)\n", ctl[W_WIDTH], ctl[W_HEIGHT]);
        return 1;
    }
    if (!(o = fopen(file, "wb")) || fwrite(img, 1, size, o) != size || fclose(o)) {
        perror(file);
        return 1;
    }
    printf("glshow: %lux%lu RGBA into %s\n", ctl[W_WIDTH], ctl[W_HEIGHT], file);
    return 0;
}

/* the picture's place in both overlay buffers to 0 (transparent): no
   leftovers of an earlier picture before the first frame */
static void clear_overlay(unsigned long pitch, int row0, int rows)
{
    const unsigned long bufs[2] = { ZZAV_OVL_OFF, ZZAV_OVL_OFF1 };
    int b, y;

    for (b = 0; b < 2; b++) {
        const unsigned long start = bufs[b] + (unsigned long)row0 * pitch;
        const unsigned long base = start & ~(PAGE - 1);
        const unsigned long len = (start - base + (unsigned long)rows * pitch + PAGE - 1) & ~(PAGE - 1);
        unsigned char *p = (unsigned char *)map(base, len);

        if (!p)
            continue;
        for (y = 0; y < rows; y++)
            memset(p + (start - base) + (unsigned long)y * pitch, 0, W * 4);
        munmap((caddr_t)p, (size_t)len);
    }
}

int main(int argc, char **argv)
{
    struct zz9k_info info;
    volatile unsigned long *ctl, *blk, *c1ld, *data;
    unsigned char *shared;
    int scene = argc > 1 && !strcmp(argv[1], "q3") ? 1 : argc > 1 && !strcmp(argv[1], "fluid") ? 2 :
                argc > 1 && !strcmp(argv[1], "q3mt") ? 3 : 0;
    unsigned long frames = argc > 2 ? strtoul(argv[2], NULL, 0) : 300;
    int x = argc > 4 ? atoi(argv[3]) : -1, y = argc > 4 ? atoi(argv[4]) : -1;
    int vs, i;
    unsigned long seq;
    double t0, tick_hz, ticks;
    char name[17];

    if (argc == 3 && !strcmp(argv[1], "-grab"))
        return grab(argv[2]);
    if (argc > 1 && strcmp(argv[1], "q3") && strcmp(argv[1], "gears") && strcmp(argv[1], "fluid") &&
        strcmp(argv[1], "q3mt")) {
        fprintf(stderr, "usage: glshow [gears|q3|q3mt|fluid] [frames] [x y]\n");
        return 1;
    }
    if ((fd = open("/dev/zz9000", O_RDWR)) < 0 || ioctl(fd, ZZ9KIOC_GETINFO, &info) < 0) {
        perror("/dev/zz9000");
        return 1;
    }
    ctl = (volatile unsigned long *)map(HB_CTL_OFF, PAGE);
    blk = (volatile unsigned long *)map(ZZAV_OFF, PAGE);
    c1ld = (volatile unsigned long *)map(C1LD_OFF, PAGE);
    data = (volatile unsigned long *)map(HB_DATA_OFF, PAGE);
    shared = (unsigned char *)map(SHARED_OFF, PAGE);
    if (!ctl || !blk || !c1ld || !data || !shared) {
        perror("mmap (needs a ZZ9K_MAP_BOARD kernel)");
        return 1;
    }
    for (i = 0; i < 4; i++) {
        unsigned long w = ctl[W_NAME + i];

        name[i * 4] = (char)(w >> 24);
        name[i * 4 + 1] = (char)(w >> 16);
        name[i * 4 + 2] = (char)(w >> 8);
        name[i * 4 + 3] = (char)w;
    }
    name[16] = 0;
    if (ctl[W_MAGIC] != HB_MAGIC_READY || strcmp(name, "glbench")) {
        fprintf(stderr, "glshow: core 1 does not run glbench (armbench -reload z3660ARM_Core_glbench.bin)\n");
        return 1;
    }
    if (ctl[W_STATE] == HB_POSTED || ctl[W_STATE] == HB_RUNNING) {
        fprintf(stderr, "glshow: core 1 is busy (state %lu)\n", ctl[W_STATE]);
        return 1;
    }

    /* older firmware shows each overlay row twice at 1920 wide (zzav.c) */
    vs = info.width >= 1600 && !(c1ld[C1LD_W_MAGIC] == C1LD_MAGIC && (c1ld[C1LD_W_FEAT] & C1LD_FEAT_OVL_FETCH)) ? 2 : 1;
    if (x < 0)
        x = (info.width - W) / 2;
    if (y < 0)
        y = (info.height - H * vs) / 2;
    if (x + W > info.width)
        x = info.width - W;
    if (y + H * vs > info.height)
        y = info.height - H * vs;
    if (x < 0 || y < 0) {
        fprintf(stderr, "glshow: the screen (%ux%u) is too small\n", info.width, info.height);
        return 1;
    }
    blk[ZZAV_W_OVL_PITCH] = (unsigned long)info.width * 4;
    blk[ZZAV_W_OVL_SHIFT] = 0;
    blk[ZZAV_W_OVL_MAXW] = W;
    blk[ZZAV_W_OVL_MAXH] = H;
    blk[ZZAV_W_OVL_ROW0] = (unsigned long)(y / vs);
    blk[ZZAV_W_OVL_DBUF] = c1ld[C1LD_W_MAGIC] == C1LD_MAGIC && (c1ld[C1LD_W_FEAT] & C1LD_FEAT_OVL_FLIP);

    /* the overlay on: MPEG INIT with nothing to decode.  Core 0 turns it on
       with the place and size it has (enable_overlay()), and takes a new
       size only once it is on (at the next vblank): place it before and
       again after */
    clear_overlay((unsigned long)info.width * 4, y / vs, H);
    place(x, y, vs);
    memset(shared, 0, 96);
    reg32(REG_MPEG_PARAM0 + 0, info.base + SHARED_OFF);
    reg32(REG_MPEG_PARAM0 + 4, info.base);
    reg32(REG_MPEG_PARAM0 + 8, FIFO_SIZE);
    reg32(REG_MPEG_PARAM0 + 12, FIFO_OFF);
    reg32(REG_MPEG_CMD, CMD_INIT);
    usleep(100000);
    place(x, y, vs);

    /* the job (glbench.c's parameters, big-endian words) */
    data[0] = 0x474C424EUL;             /* "GLBN" */
    data[1] = (unsigned long)scene;
    data[2] = scene == 2 ? W / 2 : W;      /* fluid: 160x120, shown twice the size */
    data[3] = scene == 2 ? H / 2 : H;
    data[4] = frames;
    data[5] = 0;                        /* q3: the default triangle count */
    data[6] = GLB_SHOW;
    seq = ctl[W_SEQ] + 1;
    ctl[W_DATA_OFF] = HB_DATA_OFF;
    ctl[W_DATA_SIZE] = 28;
    ctl[W_FLAGS] = 0;
    ctl[W_SEQ] = seq;
    ctl[W_STATE] = HB_POSTED;
    printf("glshow: %s, %lu frames at %d,%d%s\n", scene == 3 ? "q3mt" : scene == 2 ? "fluid" : scene ? "q3" : "gears", frames, x, y,
           blk[ZZAV_W_OVL_DBUF] ? ", double-buffered" : "");
    t0 = now();
    while (ctl[W_STATE] != HB_DONE && ctl[W_STATE] != HB_ERROR) {
        if (ctl[W_FAULT]) {
            fprintf(stderr, "glshow: core 1 stopped with exception %lu at pc %08lx, address %08lx\n",
                    ctl[W_FAULT], ctl[W_FAULT_PC], ctl[W_FAULT_ADDR]);
            break;
        }
        if (now() - t0 > 600) {
            fprintf(stderr, "glshow: no end after 10 minutes\n");
            break;
        }
        usleep(100000);
    }
    tick_hz = (double)ctl[W_TICK_HZ];
    ticks = (double)ctl[W_TICKS_HI] * 4294967296.0 + (double)ctl[W_TICKS_LO];
    if (ctl[W_STATE] == HB_DONE && ticks > 0 && tick_hz > 0)
        printf("glshow: %lu frames in %.2f s = %.1f fps (shown on the overlay)\n", ctl[W_FRAMES],
               ticks / tick_hz, ctl[W_FRAMES] / (ticks / tick_hz));
    else if (ctl[W_STATE] == HB_ERROR)
        fprintf(stderr, "glshow: core 1 reports error %ld\n", (long)ctl[W_ERRCODE]);
    sleep(1);                           /* the last frame a moment longer */
    reg32(REG_MPEG_CMD, CMD_CLOSE);     /* the overlay off */
    return ctl[W_STATE] == HB_DONE ? 0 : 1;
}
