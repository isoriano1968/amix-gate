/* mapsleep.c -- does sleeping cost CPU time while board memory past the
   framebuffer is mapped (ZZ9K_MAP_BOARD)?  mapsleep [map|open|none] */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/times.h>

int main(int argc, char **argv)
{
    const char *m = argc > 1 ? argv[1] : "map";
    struct tms t;
    int fd = -1, i;
    volatile unsigned char *p = 0;

    if (strcmp(m, "none"))
        fd = open("/dev/zz9000", O_RDWR);
    if (!strcmp(m, "map") || !strcmp(m, "touch")) {
        p = (volatile unsigned char *)mmap(0, 0x101000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)0x7800000);
        if (p == (volatile unsigned char *)-1) { perror("mmap"); return 1; }
        if (!strcmp(m, "touch"))
            for (i = 0; i < 0x101000; i += 4096)
                (void)p[i];
    }
    for (i = 0; i < 100; i++)
        usleep(50000);
    times(&t);
    printf("%s: user %ld sys %ld ticks for 5 s asleep\n", m, (long)t.tms_utime, (long)t.tms_stime);
    return 0;
}
