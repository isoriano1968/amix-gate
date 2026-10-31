/*
 * zz9k_reg.c - raw register peek/poke through ZZ9KIOC_GETREG/SETREG.
 *
 *   zz9k_reg /dev/zz9000 get <offset>
 *   zz9k_reg /dev/zz9000 set <offset> <value>
 *
 * Offsets and values are hex.  A 16-bit lane is read or written; on a
 * board with 32-bit registers a set of the HI lane (offset & 2 == 0) is
 * latched and the following set of the LO lane writes the whole register.
 * The driver bounds the offset by the board's register block (EINVAL
 * beyond it), which this tool also serves to demonstrate.
 */

#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include "../zz9000.h"

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
        else return 0xffffffffUL;
    }
    return v;
}

int
main(argc, argv)
int argc;
char **argv;
{
    int fd;
    struct zz9k_reg zr;

    if (argc < 4) {
        printf("usage: zz9k_reg /dev/zz9000 get <hexoff> | set <hexoff> <hexval>\n");
        return 2;
    }
    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror(argv[1]);
        return 1;
    }
    zr.offset = (unsigned short)hexval(argv[3]);
    zr.value = 0;
    if (argv[2][0] == 'g') {
        if (ioctl(fd, ZZ9KIOC_GETREG, &zr) < 0) {
            perror("ZZ9KIOC_GETREG");
            close(fd);
            return 1;
        }
        printf("reg 0x%04x = 0x%04x\n", zr.offset, zr.value);
    } else if (argv[2][0] == 's' && argc > 4) {
        zr.value = (unsigned short)hexval(argv[4]);
        if (ioctl(fd, ZZ9KIOC_SETREG, &zr) < 0) {
            perror("ZZ9KIOC_SETREG");
            close(fd);
            return 1;
        }
        printf("reg 0x%04x <= 0x%04x\n", zr.offset, zr.value);
    } else {
        printf("usage: zz9k_reg /dev/zz9000 get <hexoff> | set <hexoff> <hexval>\n");
        close(fd);
        return 2;
    }
    close(fd);
    return 0;
}
