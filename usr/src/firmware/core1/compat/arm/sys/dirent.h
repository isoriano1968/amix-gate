/* sys/dirent.h -- newlib for bare metal has none: the types its dirent.h
   needs, for code that names directories but never opens one on core 1
   (opendir and friends are not linked) */
#ifndef _SYS_DIRENT_H
#define _SYS_DIRENT_H

typedef struct _dir DIR;
struct dirent {
    long d_ino;
    char d_name[256];
};

#endif
