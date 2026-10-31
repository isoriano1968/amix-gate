/*
 * zz9k_cat.c - copy standard input to the framebuffer console.
 *
 *   command | zz9k_cat [/dev/zz9000]
 *
 * Enables the console (redirect) if it is not on, then hands every byte
 * read to ZZ9KIOC_CONPUTS.  Text and ANSI colour sequences render the way
 * the kernel's own console output does.  The console stays enabled on
 * exit so successive commands keep appending.
 */

#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include "../zz9000.h"

int
main(argc, argv)
int argc;
char **argv;
{
    char *devname;
    char buf[512];
    int fd, n;
    struct zz9k_conputs cp;

    devname = "/dev/zz9000";
    if (argc > 1)
        devname = argv[1];
    fd = open(devname, O_RDWR);
    if (fd < 0) {
        perror(devname);
        return 1;
    }
    cp.buf = buf;
    cp.len = 0;
    if (ioctl(fd, ZZ9KIOC_CONPUTS, &cp) < 0) {
        if (ioctl(fd, ZZ9KIOC_CONSOLE, ZZ9K_CONSOLE_REDIRECT) < 0) {
            perror("ZZ9KIOC_CONSOLE redirect");
            close(fd);
            return 1;
        }
    }
    while ((n = read(0, buf, sizeof buf)) > 0) {
        cp.buf = buf;
        cp.len = (unsigned long)n;
        if (ioctl(fd, ZZ9KIOC_CONPUTS, &cp) < 0) {
            perror("ZZ9KIOC_CONPUTS");
            close(fd);
            return 1;
        }
    }
    close(fd);
    return 0;
}
