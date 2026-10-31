/*
 * zz9k_rw.c - exact read(2)/write(2) bounds test on /dev/zz9000.
 *
 *   zz9k_rw /dev/zz9000 read  <hexoff> <hexlen>
 *   zz9k_rw /dev/zz9000 write <hexoff> <hexlen> [hexbyte]
 *
 * lseek()s to the offset, moves len bytes in one call, and prints the
 * byte count or errno -- no dd semantics in the way.  A read prints the
 * first eight bytes.  A write fills with the given byte (default 0).
 */

#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>

static unsigned long
hexval(s)
char *s;
{
    unsigned long v;
    char c;

    v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    while ((c = *s++) != 0) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
    }
    return v;
}

static char buf[65536];

int
main(argc, argv)
int argc;
char **argv;
{
    int fd, n, i;
    unsigned long off, len;

    if (argc < 5) {
        printf("usage: zz9k_rw /dev/zz9000 read|write <hexoff> <hexlen> [hexbyte]\n");
        return 2;
    }
    off = hexval(argv[3]);
    len = hexval(argv[4]);
    if (len > sizeof buf)
        len = sizeof buf;
    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror(argv[1]);
        return 1;
    }
    if (lseek(fd, (long)off, 0) == -1L) {
        printf("lseek 0x%lx: errno %d\n", off, errno);
        close(fd);
        return 1;
    }
    if (argv[2][0] == 'r') {
        n = read(fd, buf, (int)len);
        if (n < 0)
            printf("read 0x%lx len 0x%lx: errno %d\n", off, len, errno);
        else {
            printf("read 0x%lx len 0x%lx: %d bytes", off, len, n);
            for (i = 0; i < n && i < 8; ++i)
                printf(" %02x", buf[i] & 0xff);
            printf("\n");
        }
    } else {
        for (i = 0; i < len; ++i)
            buf[i] = (argc > 5) ? (char)hexval(argv[5]) : 0;
        n = write(fd, buf, (int)len);
        if (n < 0)
            printf("write 0x%lx len 0x%lx: errno %d\n", off, len, errno);
        else
            printf("write 0x%lx len 0x%lx: %d bytes\n", off, len, n);
    }
    close(fd);
    return (n < 0) ? 1 : 0;
}
