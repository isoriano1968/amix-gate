/*
 * armbench.c -- run arm/h264bench on the Z3660's core 1 from AMIX: copy an
 * H.264 Annex B clip into board memory, post the job, wait, print the
 * decoder's speed and checksum (compare with h264bench-host on the PC).
 *
 *   armbench [-nodeblock] clip.h264
 *   armbench -dump        (the control block's words, raw, then the core's
 *                          name, version and parts)
 *   armbench -load z3660ARM_Core_h264bench.bin    (core 0 starts core 1 with this image:
 *                                                once per power cycle)
 *   armbench -reload [-force] z3660ARM_Core_xxx.bin (the running image replaces itself:
 *                                                any number of times, no power cycle;
 *                                                -force for a hung gpu core)
 *   armbench -probe       (core 1 samples the audio formatter's DMA position)
 *   armbench -audioon     (core 0 starts the HDMI sound DMA on ZZAV_AUDIO_OFF, as the
 *                         AHI driver does; AMIX has none, so it is off after boot)
 *   armbench -stop        (refused by core 0's loader v2: it never resets core 1)
 *
 * Needs the zz9000 driver with ZZ9K_MAP_BOARD and the test firmware
 * (core 0 starts core 1 at 0x38000000); see arm/h264bench/bench.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include "../cores/h264bench/bench.h"
#include "../cores/h264bench/zzav.h"

#define FB_PHYS 0x00200000UL    /* /dev/zz9000 offset 0 = board + 2 MB */
#define PAGE    4096UL

static double now(void)
{
    struct timeval tv;

    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static void *map(int fd, unsigned long off, unsigned long len)
{
    void *p = mmap(0, (size_t)len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)(off - FB_PHYS));

    return p == (void *)-1 ? NULL : p;
}

static unsigned long le32(const unsigned char *p)
{
    return p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

/* hand a command to core 0's loader and wait until it has taken it */
static int c1ld_command(volatile unsigned long *ld, unsigned long cmd)
{
    double t0 = now();
    long status;

    ld[C1LD_W_STATUS] = 0;
    ld[C1LD_W_CMD] = cmd;
    while (ld[C1LD_W_CMD] != 0) {
        if (now() - t0 > 5) {
            fprintf(stderr, "core 0 did not take the command in 5 s\n");
            ld[C1LD_W_CMD] = 0;
            return -1;
        }
        usleep(20000);
    }
    status = (long)ld[C1LD_W_STATUS];
    if (status < 0)
        fprintf(stderr, "core 0 refused: %ld (%s)\n", status,
                status == -1 ? "image not in board memory" : status == -2 ? "load address outside core 1's region"
                : status == -3 ? "entry outside the image"
                : status == -4 ? "core 1 runs already: another image needs a power cycle"
                : status == -5 ? "this loader cannot stop core 1" : "?");
    return status < 0 ? -1 : 0;
}

/* -reload: the running h264bench copies the new image over itself and starts it */
static int reload(int fd, const char *file, int force)
{
    volatile unsigned long *ctl = (volatile unsigned long *)map(fd, HB_CTL_OFF, PAGE);
    unsigned char *data;
    unsigned long size;
    struct stat st;
    double t0;
    int f, gone = 0;

    /* a finished job (done/error) leaves core 1 waiting as well; -force for a
       hung gpu core: its timer takes the reload within a second */
    if (!ctl || ctl[W_MAGIC] != HB_MAGIC_READY ||
        (!force && ctl[W_STATE] != HB_IDLE && ctl[W_STATE] != HB_DONE && ctl[W_STATE] != HB_ERROR)) {
        fprintf(stderr, "core 1 is not an idle h264bench (mark %08lx, state %lu): -load it first\n",
                ctl ? ctl[W_MAGIC] : 0, ctl ? ctl[W_STATE] : 0);
        return 1;
    }
    if ((f = open(file, O_RDONLY)) < 0 || fstat(f, &st) < 0) {
        perror(file);
        return 1;
    }
    size = (unsigned long)st.st_size;
    data = size >= 8 && size <= HB_IMAGE_MAX ?
           (unsigned char *)map(fd, HB_DATA_OFF, (size + PAGE - 1) & ~(PAGE - 1)) : NULL;
    if (!data || read(f, data, (size_t)size) != (ssize_t)size || le32(data) != 0x48323634) {
        fprintf(stderr, "%s: not a core 1 image (no \"H264\" mark, or over %lu bytes)\n", file, HB_IMAGE_MAX);
        return 1;
    }
    close(f);
    ctl[W_DATA_OFF] = HB_DATA_OFF;
    ctl[W_DATA_SIZE] = size;
    ctl[W_FLAGS] = le32(data + 4);
    ctl[W_STATE] = HB_RELOAD;
    for (t0 = now(); now() - t0 < 5; usleep(20000)) {
        if (ctl[W_MAGIC] != HB_MAGIC_READY)
            gone = 1;
        else if (gone)
            break;
        if (ctl[W_STATE] == HB_ERROR) {
            fprintf(stderr, "core 1 refused the image (code %ld)\n", (long)ctl[W_ERRCODE]);
            return 1;
        }
    }
    if (ctl[W_FAULT])
        printf("core 1 stopped with exception %lu at pc %08lx, address %08lx (start-up stage %lu)\n",
               ctl[W_FAULT], ctl[W_FAULT_PC], ctl[W_FAULT_ADDR], ctl[W_STAGE]);
    else if (gone && ctl[W_MAGIC] == HB_MAGIC_READY)
        printf("%s: %lu bytes, core 1 restarted with it, ARM at %lu MHz\n", file, size, ctl[W_CPU_HZ] / 1000000);
    else
        printf("no restart seen after 5 s (mark %08lx, start-up stage %lu)\n", ctl[W_MAGIC], ctl[W_STAGE]);
    return gone && ctl[W_MAGIC] == HB_MAGIC_READY ? 0 : 1;
}

/* the zz9000 driver's register write (zz9000.h): 16 bits, as zzmpeg.c */
#define ZZ9KIOC_SETREG      (('Z' << 8) | 7)
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

/* REG_ZZ_AUDIO_PARAM 0 (the tx buffer), REG_ZZ_AUDIO_VAL its offset: core 0
   clears the ring and starts the DMA from its main loop */
static int audio_on(int fd)
{
    reg32(fd, 0x1EC, 0);
    reg32(fd, 0x1F0, ZZAV_AUDIO_OFF);
    usleep(200000);
    printf("sound DMA asked for at board offset %08lx\n", ZZAV_AUDIO_OFF);
    return 0;
}

/* -probe: what the audio formatter's transfer count does, 2000 times a second */
static int probe(int fd)
{
    volatile unsigned long *av = (volatile unsigned long *)map(fd, ZZAV_OFF, PAGE);
    volatile unsigned long *pr = (volatile unsigned long *)map(fd, ZZAV_PROBE_OFF, PAGE);
    unsigned long mn = 0xFFFFFFFFUL, mx = 0, back = 0, i, n;
    double t0;

    if (!av || !pr || av[ZZAV_W_MAGIC] != ZZAV_MAGIC || av[ZZAV_W_STATE] != ZZAV_IDLE || av[ZZAV_W_CMD]) {
        fprintf(stderr, "core 1's player is not there or not idle\n");
        return 1;
    }
    pr[0] = 0;
    av[ZZAV_W_CMD] = ZZAV_CMD_PROBE;
    for (t0 = now(); pr[0] != 0x50524F42UL && now() - t0 < 3; )
        usleep(20000);
    if (pr[0] != 0x50524F42UL) {
        fprintf(stderr, "no probe result\n");
        return 1;
    }
    n = pr[1];
    printf("MM2S ctrl %08lx sts %08lx period_config %08lx buff_addr %08lx bytes_per_ch %08lx\n",
           pr[3], pr[4], pr[5], pr[6], pr[7]);
    for (i = 0; i < n; i++) {
        unsigned long v = pr[8 + i];

        if (v < mn) mn = v;
        if (v > mx) mx = v;
        if (i && v < pr[8 + i - 1])
            back++;
        if (i < 24 || i % 32 == 0)
            printf("%s%lu", i % 8 ? " " : i ? "\n  " : "  ", v);
    }
    printf("\n%lu samples %lu us apart: min %lu max %lu, %lu steps back\n", n, pr[2], mn, mx, back);
    return 0;
}

/* -load / -stop: core 0's loader */
static int loader(int fd, const char *file)
{
    volatile unsigned long *ld = (volatile unsigned long *)map(fd, C1LD_OFF, PAGE);
    volatile unsigned long *ctl = (volatile unsigned long *)map(fd, HB_CTL_OFF, PAGE);
    unsigned char *data;
    unsigned long size;
    struct stat st;
    double t0;
    int f, i;

    if (!ld || !ctl) {
        perror("mmap (needs a ZZ9K_MAP_BOARD kernel)");
        return 1;
    }
    if (ld[C1LD_W_MAGIC] != C1LD_MAGIC) {
        fprintf(stderr, "core 0 has no core 1 loader (mark %08lx): not the loader firmware?\n", ld[C1LD_W_MAGIC]);
        return 1;
    }
    if (!file)
        return c1ld_command(ld, C1LD_CMD_STOP) < 0;
    if (ld[C1LD_W_COUNT]) {
        fprintf(stderr, "core 0 has started core 1 already (%lu): another image needs a power cycle\n",
                ld[C1LD_W_COUNT]);
        return 1;
    }
    if ((f = open(file, O_RDONLY)) < 0 || fstat(f, &st) < 0) {
        perror(file);
        return 1;
    }
    size = (unsigned long)st.st_size;
    if (size < 8 || size > HB_IMAGE_MAX) {
        fprintf(stderr, "%s: %lu bytes, at most %lu\n", file, size, HB_IMAGE_MAX);
        return 1;
    }
    data = (unsigned char *)map(fd, HB_DATA_OFF, (size + PAGE - 1) & ~(PAGE - 1));
    if (!data || read(f, data, (size_t)size) != (ssize_t)size) {
        perror(file);
        return 1;
    }
    close(f);
    if (le32(data) != 0x48323634) {
        fprintf(stderr, "%s: no \"H264\" mark at the start: not h264bench.bin?\n", file);
        return 1;
    }
    for (i = 0; i < W_NWORDS; i++)      /* so what core 1 writes next is from the new image */
        ctl[i] = 0;
    ld[C1LD_W_SRC] = HB_DATA_OFF;
    ld[C1LD_W_SIZE] = size;
    ld[C1LD_W_ADDR] = C1LD_LOAD_ADDR;
    ld[C1LD_W_ENTRY] = le32(data + 4);
    if (c1ld_command(ld, C1LD_CMD_LOAD) < 0)
        return 1;
    printf("%s: %lu bytes loaded, core 1 started at %08lx (load %lu since boot)\n",
           file, size, le32(data + 4), ld[C1LD_W_COUNT]);
    for (t0 = now(); now() - t0 < 3; usleep(100000))
        if (ctl[W_MAGIC] == HB_MAGIC_READY || ctl[W_FAULT])
            break;
    if (ctl[W_FAULT])
        printf("core 1 stopped with exception %lu at pc %08lx, address %08lx (start-up stage %lu)\n",
               ctl[W_FAULT], ctl[W_FAULT_PC], ctl[W_FAULT_ADDR], ctl[W_STAGE]);
    else if (ctl[W_MAGIC] == HB_MAGIC_READY)
        printf("h264bench is ready, ARM at %lu MHz\n", ctl[W_CPU_HZ] / 1000000);
    else
        printf("no ready mark after 3 s: start-up stage %lu of 5\n", ctl[W_STAGE]);
    return ctl[W_MAGIC] == HB_MAGIC_READY ? 0 : 1;
}

int main(int argc, char **argv)
{
    volatile unsigned long *ctl;
    unsigned char *data;
    const char *file = NULL;
    unsigned long flags = 0, size, seq, last = (unsigned long)-1;
    double t0, t1, wall;
    struct stat st;
    int fd, f, i, dump = 0, load = 0, rel = 0, prb = 0, aon = 0, force = 0;

    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-nodeblock"))
            flags |= HB_FLAG_NODEBLOCK;
        else if (!strcmp(argv[i], "-dump"))
            dump = 1;
        else if (!strcmp(argv[i], "-load"))
            load = 1;
        else if (!strcmp(argv[i], "-stop"))
            load = 2;
        else if (!strcmp(argv[i], "-reload"))
            rel = 1;
        else if (!strcmp(argv[i], "-force"))
            force = 1;
        else if (!strcmp(argv[i], "-probe"))
            prb = 1;
        else if (!strcmp(argv[i], "-audioon"))
            aon = 1;
        else
            file = argv[i];
    if (rel || prb || aon) {
        if ((fd = open("/dev/zz9000", O_RDWR)) < 0) {
            perror("/dev/zz9000");
            return 1;
        }
        if (aon)
            return audio_on(fd);
        if (rel && !file) {
            fprintf(stderr, "usage: armbench -reload [-force] z3660ARM_Core_xxx.bin\n");
            return 2;
        }
        return rel ? reload(fd, file, force) : probe(fd);
    }
    if (load) {
        if (load == 1 && !file) {
            fprintf(stderr, "usage: armbench -load h264bench.bin\n");
            return 2;
        }
        if ((fd = open("/dev/zz9000", O_RDWR)) < 0) {
            perror("/dev/zz9000");
            return 1;
        }
        return loader(fd, load == 1 ? file : NULL);
    }
    if (dump) {
        if ((fd = open("/dev/zz9000", O_RDWR)) < 0 || !(ctl = (volatile unsigned long *)map(fd, HB_CTL_OFF, PAGE))) {
            perror("/dev/zz9000");
            return 1;
        }
        for (i = 0; i < 40; i++)
            printf("%08lx%c", ctl[i], i % 8 == 7 ? '\n' : ' ');
        if (ctl[W_MAGIC] == HB_MAGIC_READY) {
            static const char *parts[] = { "gl", "h264", "crypto", "net", "glbench", "netbench", "uae" };
            unsigned long v = ctl[W_VERSION];
            char name[17];

            for (i = 0; i < 16; i++) {          /* big-endian words: the bytes in order */
                name[i] = (char)(ctl[W_NAME + i / 4] >> (24 - 8 * (i % 4)));
                if (name[i] < ' ' || name[i] > '~')
                    name[i] = 0;
            }
            name[16] = 0;
            if (v && v <= 0xffffff) {
                printf("core: %s %lu.%lu.%lu [", name[0] ? name : "h264bench", v >> 16, (v >> 8) & 0xff, v & 0xff);
                for (i = 0, f = 0; i < 7; i++)
                    if (ctl[W_PARTS] & (1UL << i))
                        printf("%s%s", f++ ? " " : "", parts[i]);
                printf("]\n");
            } else
                printf("core: %s (from before versions)\n", name[0] ? name : "h264bench");
        }
        if ((ctl = (volatile unsigned long *)map(fd, C1LD_OFF, PAGE)) != NULL)
            printf("loader: %08lx cmd %08lx status %ld loads %lu\n", ctl[C1LD_W_MAGIC], ctl[C1LD_W_CMD],
                   (long)ctl[C1LD_W_STATUS], ctl[C1LD_W_COUNT]);
        return 0;
    }
    if (!file || (f = open(file, O_RDONLY)) < 0 || fstat(f, &st) < 0) {
        fprintf(stderr, "usage: armbench [-nodeblock] clip.h264 | -load|-reload z3660ARM_Core_xxx.bin | -probe | -dump\n");
        return 2;
    }
    size = (unsigned long)st.st_size;
    if (size == 0 || size > HB_DATA_MAX) {
        fprintf(stderr, "%s: %lu bytes, at most %lu\n", file, size, HB_DATA_MAX);
        return 1;
    }
    fd = open("/dev/zz9000", O_RDWR);
    if (fd < 0) {
        perror("/dev/zz9000");
        return 1;
    }
    ctl = (volatile unsigned long *)map(fd, HB_CTL_OFF, PAGE);
    data = (unsigned char *)map(fd, HB_DATA_OFF, (size + PAGE - 1) & ~(PAGE - 1));
    if (!ctl || !data) {
        perror("mmap (needs a ZZ9K_MAP_BOARD kernel)");
        return 1;
    }
    if (ctl[W_FAULT]) {
        fprintf(stderr, "core 1 stopped with exception %lu at pc %08lx, address %08lx\n",
                ctl[W_FAULT], ctl[W_FAULT_PC], ctl[W_FAULT_ADDR]);
        return 1;
    }
    if (ctl[W_MAGIC] != HB_MAGIC_READY) {
        fprintf(stderr, "core 1 is not running h264bench (mark %08lx, start-up stage %lu of 5): not the test firmware?\n",
                ctl[W_MAGIC], ctl[W_STAGE]);
        return 1;
    }
    if (ctl[W_STATE] == HB_POSTED || ctl[W_STATE] == HB_RUNNING) {
        fprintf(stderr, "core 1 is busy with another job (state %lu)\n", ctl[W_STATE]);
        return 1;
    }
    /* the 060 is big-endian like the block: no swapping on this side */
    if (read(f, data, (size_t)size) != (ssize_t)size) {
        perror(file);
        return 1;
    }
    close(f);
    {
        unsigned long b0 = ctl[W_HEARTBEAT], n0 = ctl[W_NOW];

        sleep(1);
        printf("%s: %lu bytes; ARM at %lu MHz, timer %lu Hz, heartbeat %lu, %lu a second, timer %lu a second\n",
               file, size, ctl[W_CPU_HZ] / 1000000, ctl[W_TICK_HZ], ctl[W_HEARTBEAT], ctl[W_HEARTBEAT] - b0,
               ctl[W_NOW] - n0);
        if (ctl[W_HEARTBEAT] == b0) {
            fprintf(stderr, "core 1's heartbeat stands still: it does not run (or its writes do not arrive)\n");
            return 1;
        }
    }
    seq = ctl[W_SEQ] + 1;
    ctl[W_DATA_OFF] = HB_DATA_OFF;
    ctl[W_DATA_SIZE] = size;
    ctl[W_FLAGS] = flags;
    ctl[W_SEQ] = seq;
    ctl[W_STATE] = HB_POSTED;
    t0 = now();
    for (;;) {
        unsigned long st2 = ctl[W_STATE], p = ctl[W_PROGRESS];

        if (st2 == HB_DONE || st2 == HB_ERROR)
            break;
        if (ctl[W_FAULT]) {
            fprintf(stderr, "core 1 stopped with exception %lu at pc %08lx, address %08lx after %lu frames\n",
                    ctl[W_FAULT], ctl[W_FAULT_PC], ctl[W_FAULT_ADDR], p);
            return 1;
        }
        if (p != last && p % 50 == 0 && p)
            printf("  %lu frames after %.1f s\n", p, now() - t0);
        last = p;
        if (now() - t0 > 600) {
            fprintf(stderr, "no result after 10 minutes (state %lu, %lu frames)\n", st2, p);
            return 1;
        }
        usleep(200000);
    }
    t1 = now();
    wall = t1 - t0;
    {
        double tick_hz = (double)ctl[W_TICK_HZ];
        double ticks = (double)ctl[W_TICKS_HI] * 4294967296.0 + (double)ctl[W_TICKS_LO];
        double dec = tick_hz > 0 ? ticks / tick_hz : 0;
        unsigned long frames = ctl[W_FRAMES];

        printf("%s: %lu frames %lux%lu, checksum %08lx, errors %lu (code %ld)%s\n",
               ctl[W_STATE] == HB_DONE ? "done" : "ERROR", frames, ctl[W_WIDTH], ctl[W_HEIGHT],
               ctl[W_CHECKSUM], ctl[W_ERRORS], (long)ctl[W_ERRCODE],
               flags & HB_FLAG_NODEBLOCK ? ", no loop filter" : "");
        printf("decode %.2f s (ARM timer) = %.1f fps, slowest frame %.1f ms; wall %.2f s for the job\n",
               dec, dec > 0 ? frames / dec : 0.0, tick_hz > 0 ? ctl[W_MAX_TICKS] / tick_hz * 1000 : 0.0,
               wall);
    }
    return ctl[W_STATE] == HB_DONE ? 0 : 1;
}
