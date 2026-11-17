/*
 * rtgcaps.h - the capabilities ioctl every AMIX RTG kernel driver answers
 * (plan row 0.8, amix-userland x11/xserver-amix docs/MODULAR-X-SERVER-
 * EVALUATION.md section 5a).  MIT licence, as the drivers.
 *
 * The X server's card modules, the CDE display panel, rc scripts and tools
 * ask the driver what the board can do before they try it: which depths it
 * can show, whether the kernel console can be drawn on it, whether it has a
 * mode list with refresh rates.  A driver that does not answer (an older
 * kernel) gives EINVAL or ENOTTY; the caller then keeps its static facts.
 *
 * Use: set version = RTGCAPS_VERSION and size = sizeof(struct rtg_caps),
 * then ioctl(fd, RTGIOC_GETCAPS, &caps).  The driver fills at most size
 * bytes, sets size to the bytes it filled and version to its own; the
 * struct only grows at the end.  ENXIO: no board behind the node.
 *
 * Drivers that cannot include this file from io/ (the zz9000 driver's own
 * build copies only zz9000.c and zz9000.h) carry the block between the
 * RTGCAPS BEGIN and RTGCAPS END lines byte for byte; the kit scripts
 * compare the copies.
 */
/* RTGCAPS BEGIN */
#ifndef RTGCAPS_H
#define RTGCAPS_H

#define RTGIOC_GETCAPS          (('R' << 8) | 1)  /* the same number in every RTG driver */
#define RTGCAPS_VERSION         1

#define RTGCAP_CONSOLE_REDIRECT 0x0001  /* the kernel console can be drawn on the board */
#define RTGCAP_MONITOR_SWITCH   0x0002  /* the driver switches the monitor Amiga/RTG */
#define RTGCAP_BLIT_FILL        0x0004  /* solid fills by ioctl (card or kernel) */
#define RTGCAP_BLIT_COPY        0x0008  /* screen-to-screen copies by ioctl */
#define RTGCAP_BLIT_PATTERN     0x0010
#define RTGCAP_HW_CURSOR        0x0020
#define RTGCAP_CLUT             0x0040  /* a palette the driver loads */
#define RTGCAP_SOFT3D           0x0080  /* the Z3660 firmware's soft3D */
#define RTGCAP_MODE_LIST        0x0100  /* a mode list by ioctl */
#define RTGCAP_REFRESH          0x0200  /* refresh rates known and selectable */
#define RTGCAP_CARD_BLITTER     0x0400  /* fills/copies run on the card */

/* depths: bit n set = depth n can be shown; 24 is 24-bit colour in a
 * 32-bit pixel (X depth 24, 32 bpp) */
#define RTG_DEPTH(n)            (1UL << (n))

struct rtg_caps {
    unsigned short version;       /* in: caller's; out: the driver's */
    unsigned short size;          /* in: bytes the caller has; out: bytes filled */
    char board[24];               /* "Z3660 RTG", "ZZ9000", "Picasso II", NUL-terminated */
    unsigned long caps;           /* RTGCAP_* */
    unsigned long depths;         /* RTG_DEPTH(8) | RTG_DEPTH(16) ... */
    unsigned long fb_size;        /* framebuffer bytes */
    unsigned long max_width;      /* largest mode in the board's list */
    unsigned long max_height;
};

#endif /* RTGCAPS_H */
/* RTGCAPS END */
