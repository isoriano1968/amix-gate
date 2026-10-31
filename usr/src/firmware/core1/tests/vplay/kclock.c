/* kclock.c -- can a program read the kernel's clock (hrestime: seconds and
   nanoseconds, advanced every tick) from a /dev/kmem mapping, instead of a
   system call for every gettimeofday?
     kclock [lo hi]   search kernel memory lo..hi (hex) for it */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>

static unsigned long find(int fd, unsigned long lo, unsigned long hi)
{
    static long buf[16384];
    unsigned long a;
    long now;
    int i, n;

    for (a = lo; a < hi; a += sizeof buf) {
        if (lseek(fd, (off_t)a, SEEK_SET) < 0 || (n = read(fd, buf, sizeof buf)) <= 0) {
            printf("%08lx: read %d\n", a, n);
            continue;
        }
        now = (long)time(0);
        for (i = 0; i + 1 < n / 4; i++)
            if (buf[i] == now || buf[i] == now - 1)
                printf("candidate %08lx: %ld.%09ld\n", a + i * 4, buf[i], buf[i + 1]);
    }
    return 0;
}

int main(int argc, char **argv)
{
    unsigned long lo = argc > 2 ? strtoul(argv[1], 0, 16) : 0x08100000UL;
    unsigned long hi = argc > 2 ? strtoul(argv[2], 0, 16) : 0x08200000UL;
    unsigned long addr, page;
    volatile long *t;
    struct timeval tv, t0, t1;
    char *p;
    int fd, i;
    long s, ns;

    fd = open("/dev/kmem", O_RDONLY);
    if (fd < 0) { perror("/dev/kmem"); return 1; }
    if (argc == 2) {
        addr = strtoul(argv[1], 0, 16);
    } else {
        find(fd, lo, hi);
        return 0;
    }
    page = addr & ~4095UL;
    p = (char *)mmap(0, 4096, PROT_READ, MAP_SHARED, fd, (off_t)page);
    if (p == (char *)-1) { perror("mmap"); return 1; }
    t = (volatile long *)(p + (addr - page));
    for (i = 0; i < 5; i++) {
        gettimeofday(&tv, 0);
        printf("kmem %ld.%09ld  gettimeofday %ld.%06ld\n", t[0], t[1], (long)tv.tv_sec, (long)tv.tv_usec);
        usleep(30000);
    }
    gettimeofday(&t0, 0);
    for (i = 0; i < 10000; i++)
        gettimeofday(&tv, 0);
    gettimeofday(&t1, 0);
    printf("gettimeofday: %.2f us a call\n", ((t1.tv_sec - t0.tv_sec) * 1e6 + t1.tv_usec - t0.tv_usec) / 10000);
    gettimeofday(&t0, 0);
    for (i = 0; i < 100000; i++) {
        do { s = t[0]; ns = t[1]; } while (s != t[0]);
    }
    gettimeofday(&t1, 0);
    printf("kmem read: %.3f us a call (%ld)\n", ((t1.tv_sec - t0.tv_sec) * 1e6 + t1.tv_usec - t0.tv_usec) / 100000, ns);
    return 0;
}
