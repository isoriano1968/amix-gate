/*
 * zz9k_maptest.c - is a user mmap of /dev/zz9000 coherent with the kernel?
 *
 *   zz9k_maptest [/dev/zz9000]
 *
 * The first thing to run on a kernel whose user device mappings are
 * unproven.  It maps the framebuffer (board_size, raising its own VM limit),
 * then for two pages 4 KiB apart:
 *   1. writes a marker through the mapping, reads it back through read(2)
 *      -- a copy-back mapping keeps the marker in the data cache and the
 *      kernel's read sees the old bytes;
 *   2. writes a marker through write(2), reads it back through the mapping;
 *   3. maps the same offset a second time and compares (aliasing).
 * Every comparison is printed; any MISMATCH means the mapping must not be
 * used to draw (use the write(2) paths instead).  Root; no mode is set,
 * the framebuffer bytes are restored afterwards.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include "../zz9000.h"

extern int getrlimit(), setrlimit();

static unsigned long
kread(fd, off)
int fd;
unsigned long off;
{
    unsigned long v;

    if (lseek(fd, (long)off, 0) == -1L)
        return 0xdead0001L;
    if (read(fd, (char *)&v, 4) != 4)
        return 0xdead0002L;
    return v;
}

static int
kwrite(fd, off, v)
int fd;
unsigned long off;
unsigned long v;
{
    if (lseek(fd, (long)off, 0) == -1L)
        return -1;
    return (write(fd, (char *)&v, 4) == 4) ? 0 : -1;
}

int
main(argc, argv)
int argc;
char **argv;
{
    char *devname, *m, *m2;
    int fd, i, fails;
    unsigned long off, v, saved[2], need;
    volatile unsigned long *p;
    struct zz9k_info info;
    struct rlimit rl;
    static unsigned long offs[2] = { 0x0L, 0x1000L };

    setbuf(stdout, (char *)0);
    devname = (argc > 1) ? argv[1] : "/dev/zz9000";
    fd = open(devname, O_RDWR);
    if (fd < 0) {
        perror(devname);
        return 1;
    }
    if (ioctl(fd, ZZ9KIOC_PROBE, 0) < 0 || ioctl(fd, ZZ9KIOC_GETINFO, &info) < 0) {
        perror("ZZ9KIOC_PROBE/GETINFO");
        return 1;
    }
    printf("board 0x%lx fb_offset 0x%lx fb_size 0x%lx\n", info.board_size,
        info.fb_offset, info.fb_size);
    if (getrlimit(RLIMIT_VMEM, &rl) == 0) {
        need = info.board_size + 0x800000;
        if ((unsigned long)rl.rlim_cur < need) {
            rl.rlim_cur = ((unsigned long)rl.rlim_max < need) ? rl.rlim_max : need;
            (void)setrlimit(RLIMIT_VMEM, &rl);
        }
    }
    m = (char *)mmap((caddr_t)0, info.board_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ((long)m == -1) {
        perror("mmap");
        return 1;
    }
    printf("mapped at %08lx\n", (unsigned long)m);
    fails = 0;
    for (i = 0; i < 2; ++i) {
        off = info.fb_offset + offs[i];
        saved[i] = kread(fd, off);
        p = (volatile unsigned long *)(m + off);

        v = 0xa5c30f00L + i;
        *p = v;
        printf("page %d: mmap write %08lx, read(2) sees %08lx  %s\n", i, v, kread(fd, off),
            (kread(fd, off) == v) ? "MATCH" : "MISMATCH (mapping not coherent with the kernel)");
        if (kread(fd, off) != v) ++fails;

        v = 0x5a3cf011L + i;
        kwrite(fd, off, v);
        printf("page %d: write(2) %08lx, mmap sees %08lx  %s\n", i, v, *p,
            (*p == v) ? "MATCH" : "MISMATCH (stale data through the mapping)");
        if (*p != v) ++fails;
    }
    m2 = (char *)mmap((caddr_t)0, 0x2000, PROT_READ, MAP_SHARED, fd, (long)info.fb_offset);
    if ((long)m2 == -1)
        printf("second mapping failed errno=%d\n", errno);
    else {
        for (i = 0; i < 2; ++i) {
            v = *(volatile unsigned long *)(m2 + offs[i]);
            printf("page %d: second mapping sees %08lx, first %08lx  %s\n", i, v,
                *(volatile unsigned long *)(m + info.fb_offset + offs[i]),
                (v == *(volatile unsigned long *)(m + info.fb_offset + offs[i])) ? "MATCH" : "MISMATCH (aliasing)");
            if (v != *(volatile unsigned long *)(m + info.fb_offset + offs[i])) ++fails;
        }
        v = *(volatile unsigned long *)(m2 + 0x1000);
        printf("pages differ through the second mapping: %s\n",
            (v != *(volatile unsigned long *)m2) ? "yes" : "NO (the two pages alias -- PFN geometry wrong)");
        if (v == *(volatile unsigned long *)m2) ++fails;
        munmap((caddr_t)m2, 0x2000);
    }
    for (i = 0; i < 2; ++i)
        kwrite(fd, info.fb_offset + offs[i], saved[i]);
    munmap((caddr_t)m, info.board_size);
    close(fd);
    printf("zz9k_maptest: %s\n", fails ? "FAIL -- do not draw through the mapping" : "PASS -- user mapping coherent");
    return fails ? 1 : 0;
}
