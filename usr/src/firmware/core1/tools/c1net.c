/*
 * c1net.c -- core 1's own network from AMIX: hand the netbench core
 * (cores/netbench) a command, wait, print its answer; or show the rings'
 * words core 0 and core 1 share (cores/h264bench/core1_net.h).
 *
 *   c1net info                      core 1's address (DHCP), MAC, counters
 *   c1net get a.b.c.d port /path    an HTTP GET by core 1: its speed
 *   c1net -rings                    the shared words, raw
 *
 * Needs the firmware with C1LD_FEAT_NET and the zz9000 driver with
 * ZZ9K_MAP_BOARD, as armbench.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include "../cores/h264bench/bench.h"
#include "../cores/h264bench/core1_net.h"

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

/* the ARM's words are little-endian */
static unsigned long le(unsigned long v)
{
    return (v >> 24) | (v >> 8 & 0xff00UL) | (v << 8 & 0xff0000UL) | (v << 24);
}

int main(int argc, char **argv)
{
    static const char *const names[C1NET_W_NWORDS] = {
        "magic", "on", "maclo", "machi", "rxwr", "rxrd", "txwr", "txrd",
        "rxframes", "rxdrops", "txframes", "txerrs"
    };
    volatile unsigned long *ctl;
    char *data, cmd[512];
    double t0;
    unsigned long seq, st;
    int fd, i;

    if (argc < 2) {
        fprintf(stderr, "usage: c1net info | get a.b.c.d port /path | -rings\n");
        return 2;
    }
    fd = open("/dev/zz9000", O_RDWR);
    if (fd < 0) {
        perror("/dev/zz9000");
        return 1;
    }
    if (!strcmp(argv[1], "-rings")) {
        volatile unsigned long *w = (volatile unsigned long *)map(fd, C1NET_OFF, PAGE);

        if (!w) {
            perror("mmap (needs a ZZ9K_MAP_BOARD kernel)");
            return 1;
        }
        for (i = 0; i < C1NET_W_NWORDS; i++)
            printf("%-9s %08lx  %lu\n", names[i], le(w[i]), le(w[i]));
        return 0;
    }
    cmd[0] = 0;
    for (i = 1; i < argc; i++) {
        if (strlen(cmd) + strlen(argv[i]) + 2 > sizeof cmd) {
            fprintf(stderr, "c1net: command too long\n");
            return 2;
        }
        if (i > 1)
            strcat(cmd, " ");
        strcat(cmd, argv[i]);
    }
    ctl = (volatile unsigned long *)map(fd, HB_CTL_OFF, PAGE);
    data = (char *)map(fd, HB_DATA_OFF, PAGE);
    if (!ctl || !data) {
        perror("mmap (needs a ZZ9K_MAP_BOARD kernel)");
        return 1;
    }
    if (ctl[W_FAULT]) {
        fprintf(stderr, "core 1 stopped with exception %lu at pc %08lx, address %08lx\n",
                ctl[W_FAULT], ctl[W_FAULT_PC], ctl[W_FAULT_ADDR]);
        return 1;
    }
    if (ctl[W_MAGIC] != HB_MAGIC_READY || memcmp((const char *)&ctl[W_NAME], "netbench", 8)) {
        fprintf(stderr, "core 1 does not run netbench (armbench -reload z3660ARM_Core_netbench.bin)\n");
        return 1;
    }
    if (ctl[W_STATE] == HB_POSTED || ctl[W_STATE] == HB_RUNNING) {
        fprintf(stderr, "core 1 is busy with another job (state %lu)\n", ctl[W_STATE]);
        return 1;
    }
    strcpy(data, cmd);
    seq = ctl[W_SEQ] + 1;
    ctl[W_DATA_OFF] = HB_DATA_OFF;
    ctl[W_DATA_SIZE] = strlen(cmd) + 1;
    ctl[W_FLAGS] = 0;
    ctl[W_SEQ] = seq;
    ctl[W_STATE] = HB_POSTED;
    t0 = now();
    while ((st = ctl[W_STATE]) != HB_DONE && st != HB_ERROR) {
        if (ctl[W_FAULT]) {
            fprintf(stderr, "core 1 stopped with exception %lu at pc %08lx, address %08lx\n",
                    ctl[W_FAULT], ctl[W_FAULT_PC], ctl[W_FAULT_ADDR]);
            return 1;
        }
        if (now() - t0 > 180) {
            fprintf(stderr, "no answer after 3 minutes (state %lu, %lu KB)\n", st, ctl[W_PROGRESS]);
            return 1;
        }
        usleep(200000);
    }
    data[PAGE - 1] = 0;
    printf("%s\n", data);
    return st == HB_DONE ? 0 : 1;
}
