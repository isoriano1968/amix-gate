/*
 * zz9000.h - MNT ZZ9000 AMIX driver definitions.
 *
 * AMIX target: Zorro II only.  AMIX does not support Zorro III, so
 * this driver deliberately probes product 3 and leaves product 4 alone.
 */

#ifndef ZZ9000_H
#define ZZ9000_H

#define ZZ9K_MNT_MANUFACTURER  0x6d6e
#define ZZ9K_PRODUCT_Z2        3
#define ZZ9K_PRODUCT_Z3        4
#define ZZ9K_PRODUCT           0x6d6e0003UL
#define ZZ9K_PRODUCT_PC_Z2     0x6d6e0003UL
#define ZZ9K_PRODUCT_PC_Z3     0x6d6e0004UL
#define ZZ9K_MAX_AUTOCON       16

#define ZZ9K_REG_HW_VERSION    0x00
#define ZZ9K_REG_MODE          0x02
#define ZZ9K_REG_CONFIG        0x04
#define ZZ9K_REG_PAN_HI        0x0a
#define ZZ9K_REG_PAN_LO        0x0c
#define ZZ9K_REG_VCAP_MODE     0x0e
#define ZZ9K_REG_VBLANK_STATUS 0x4c
#define ZZ9K_REG_FW_VERSION    0xc0

#define ZZ9K_REG_CX_DATA_HI    0x1000
#define ZZ9K_REG_CX_DATA_LO    0x1002
#define ZZ9K_REG_CX_OP         0x1004
#define ZZ9K_REG_CX_CAPTURE    0x1006

#define ZZ9K_REG_BLITTER_X1    0x0010
#define ZZ9K_REG_BLITTER_Y1    0x0012
#define ZZ9K_REG_BLITTER_X2    0x0014
#define ZZ9K_REG_BLITTER_Y2    0x0016
#define ZZ9K_REG_BLITTER_ROW_PITCH 0x0018
#define ZZ9K_REG_BLITTER_X3    0x001a
#define ZZ9K_REG_BLITTER_Y3    0x001c
#define ZZ9K_REG_BLITTER_RGB_HI 0x001e
#define ZZ9K_REG_BLITTER_RGB_LO 0x0020
#define ZZ9K_REG_BLITTER_OP_FILLRECT 0x0022
#define ZZ9K_REG_BLITTER_OP_COPYRECT 0x0024
#define ZZ9K_REG_BLITTER_SRC_HI 0x0028
#define ZZ9K_REG_BLITTER_SRC_LO 0x002a
#define ZZ9K_REG_BLITTER_DST_HI 0x002c
#define ZZ9K_REG_BLITTER_DST_LO 0x002e
#define ZZ9K_REG_BLITTER_COLORMODE 0x0030
#define ZZ9K_REG_BLITTER_SRC_PITCH 0x0032
#define ZZ9K_REG_BLITTER_SPLIT_POS 0x005e

#define ZZ9K_REG_SIZE          0x00002000UL
#define ZZ9K_FB_OFFSET         0x00010000UL
#define ZZ9K_Z2_RESERVED_TAIL  0x00020000UL
#define ZZ9K_FW_VER_MIN        0x010d

#define ZZ9K_COLOR_8BIT        0
#define ZZ9K_COLOR_16BIT565    1
#define ZZ9K_COLOR_32BIT       2
#define ZZ9K_COLOR_15BIT       3

#define ZZ9K_MODE_1280x720     0
#define ZZ9K_MODE_800x600      1
#define ZZ9K_MODE_640x480      2
#define ZZ9K_MODE_1024x768     3
#define ZZ9K_MODE_1280x1024    4
#define ZZ9K_MODE_1920x1080    5
#define ZZ9K_MODE_720x576p50   6
#define ZZ9K_MODE_720x576      ZZ9K_MODE_720x576p50
#define ZZ9K_MODE_1920x1080p50 7
#define ZZ9K_MODE_720x480      8
#define ZZ9K_MODE_640x512      9
#define ZZ9K_MODE_1600x1200    10
#define ZZ9K_MODE_2560x1440p30 11
#define ZZ9K_MODE_2560x1440    ZZ9K_MODE_2560x1440p30
#define ZZ9K_MODE_1920x1080_NS 12
#define ZZ9K_MODE_1280x720_NS  13
#define ZZ9K_MODE_800x600_NS   14
#define ZZ9K_MODE_640x480_NS   15
#define ZZ9K_MODE_640x400      16
#define ZZ9K_MODE_1920x800     17

#define ZZ9K_SWITCH_RTG        1
#define ZZ9K_SWITCH_CAPTURE    0

#define ZZ9K_CAPTURE_PAN       0x00e00000UL
#define ZZ9K_CAPTURE_PAN_VGA   0x00dff2f8UL

#define ZZ9K_IOC               ('Z' << 8)
#define ZZ9KIOC_GETINFO        (ZZ9K_IOC | 1)
#define ZZ9KIOC_SETMODE        (ZZ9K_IOC | 2)
#define ZZ9KIOC_SETPAN         (ZZ9K_IOC | 3)
#define ZZ9KIOC_SETSWITCH      (ZZ9K_IOC | 4)
#define ZZ9KIOC_FILL           (ZZ9K_IOC | 5)
#define ZZ9KIOC_GETREG         (ZZ9K_IOC | 6)
#define ZZ9KIOC_SETREG         (ZZ9K_IOC | 7)
#define ZZ9KIOC_GETAUTOCON     (ZZ9K_IOC | 8)
#define ZZ9KIOC_PROBE          (ZZ9K_IOC | 9)
#define ZZ9KIOC_CONSOLE        (ZZ9K_IOC | 10)
#define ZZ9KIOC_GETFBINFO      (ZZ9K_IOC | 11)
#define ZZ9KIOC_FILLRECT       (ZZ9K_IOC | 12)
#define ZZ9KIOC_COPYRECT       (ZZ9K_IOC | 13)
#define ZZ9KIOC_CONPUTS        (ZZ9K_IOC | 14)  /* write text to the framebuffer console */
#define ZZ9KIOC_GETMONSW       (ZZ9K_IOC | 15)  /* interim only, see below */
#define ZZ9KIOC_BLITS          (ZZ9K_IOC | 16)  /* a batch of card-blitter fills/copies */
/*
 * ZZ9KIOC_GETMONSW was answered by the interim RT-03 driver (03844ac, 407d379) that drove the
 * monitor-switch line itself; it is kept here for zz9k_monsw, which still meets such kernels.
 * Since RT-04 the line belongs to the kernel's monsw driver (sys/monsw.h) and this driver no
 * longer answers it.
 *
 * The RTG contract's monitor switch (Klaus Burckert's Xsvga-class numbering, as in Antti's
 * va2000.h): the X servers' rtg/ layer issues Set at screen init (SVGA) and at exit (Amiga)
 * on this device; `int *` argument.
 */
#ifndef SVGAIOCSetMonitorSwitch
#define SVGAIOC                 (0xe300)
#define SVGAIOCGetMonitorSwitch (SVGAIOC|0x06)
#define SVGAIOCSetMonitorSwitch (SVGAIOC|0x08)  /* note: 0x08 not 0x07 */
#define SVGAMONITORSWITCH_Amiga 1
#define SVGAMONITORSWITCH_SVGA  2
#endif

#define ZZ9K_CONSOLE_DISABLE   0
#define ZZ9K_CONSOLE_ENABLE    1
#define ZZ9K_CONSOLE_TEST      2
#define ZZ9K_CONSOLE_REDIRECT  3
#define ZZ9K_CONSOLE_MIRROR    ZZ9K_CONSOLE_REDIRECT


struct zz9k_autocon_entry {
    unsigned short manufacturer;
    unsigned short product;
    unsigned short type;
    unsigned short flags;
    unsigned long board_addr;
    unsigned long board_size;
    unsigned short slot_addr;
    unsigned short slot_size;
};

struct zz9k_autocon_dump {
    unsigned short count;
    struct zz9k_autocon_entry entry[ZZ9K_MAX_AUTOCON];
};
struct zz9k_info {
    unsigned long base;
    unsigned long board_size;
    unsigned long fb_offset;
    unsigned long fb_size;
    unsigned short hw_version;
    unsigned short fw_version;
    unsigned short product;
    unsigned short zorro;
    unsigned short width;
    unsigned short height;
    unsigned short color_mode;
    unsigned short mode;
};

struct zz9k_fbinfo {
    unsigned long fb_offset;
    unsigned long fb_size;
    unsigned short width;
    unsigned short height;
    unsigned short color_mode;
    unsigned short depth;
    unsigned short bytes_per_pixel;
    unsigned short stride;
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
    unsigned long alpha_mask;
    unsigned short red_shift;
    unsigned short green_shift;
    unsigned short blue_shift;
    unsigned short alpha_shift;
};

struct zz9k_mode {
    unsigned short width;
    unsigned short height;
    unsigned short color_mode;
    unsigned short scale_mode;
};

struct zz9k_pan {
    unsigned long offset;
};

struct zz9k_fill {
    unsigned long offset;
    unsigned long bytes;
    unsigned long value;
};

struct zz9k_fillrect {
    unsigned short x;
    unsigned short y;
    unsigned short width;
    unsigned short height;
    unsigned long color;
};

struct zz9k_copyrect {
    unsigned short src_x;
    unsigned short src_y;
    unsigned short dst_x;
    unsigned short dst_y;
    unsigned short width;
    unsigned short height;
};

/*
 * ZZ9KIOC_BLITS: n records, each a solid fill or a screen-to-screen copy in
 * pixels of the current mode, run in order by the card's own blitter.  Only
 * a board whose row carries ZZ9K_BT_BLITTER answers it (ENXIO otherwise, and
 * in a mode the blitter is not used for), so a caller falls back to its own
 * CPU path.  Every record is checked against the screen before it runs; the
 * first bad one stops the batch with EINVAL (the records before it have
 * run); on success the ioctl returns n.  Copies may overlap in either
 * direction.
 */
#define ZZ9K_BLIT_FILL         1
#define ZZ9K_BLIT_COPY         2
#define ZZ9K_BLITS_MAX         1024    /* records per call */

struct zz9k_blit {
    unsigned short op;            /* ZZ9K_BLIT_FILL or ZZ9K_BLIT_COPY */
    unsigned short x;             /* destination */
    unsigned short y;
    unsigned short width;
    unsigned short height;
    unsigned short src_x;         /* COPY */
    unsigned short src_y;
    unsigned short pad;
    unsigned long color;          /* FILL: the pixel value */
};

struct zz9k_blits {
    struct zz9k_blit *recs;
    unsigned long n;
};

struct zz9k_reg {
    unsigned short offset;
    unsigned short value;
};

/* ZZ9KIOC_CONPUTS: len bytes from buf go through the console renderer
 * (ANSI sequences included); the console must be enabled (ENXIO otherwise). */
struct zz9k_conputs {
    char *buf;
    unsigned long len;
};

/*
 * Z3660 RTG (target build -DZZ9K_TARGET_Z3660).
 *
 * The Z3660 firmware keeps the ZZ9000 register semantics -- the MODE word,
 * the colour modes, the blitter registers, PAN as a framebuffer byte offset
 * -- but exposes them as a 32-bit-word register file starting at 0x100
 * (firmware Z3660/src/rtg/zzregs.h), with no HW_VERSION, one PAN register,
 * FW_VERSION at 0x1a0 reading (major << 8) | minor, and no native-video
 * capture (REG_ZZ_OP_CAPTUREMODE 0x30c is a stub).  Registers must be
 * accessed as aligned 32-bit words: the firmware dispatches the whole
 * aligned longword of any write (rtg.c rtg_thread).  0x1000-0x1fff is a
 * firmware test area that logs every access on its UART; the driver keeps
 * it out of the device space (ZZ9K_Z3660_REG_SIZE).
 *
 * With autoconfig_rtg NO (every AMIX preset) the RTG/ethernet/piscsi combo
 * window is hard-mapped at 0x10000000, 128 MB, with no AutoConfig entry
 * (cpu_emulator.cpp reset_autoconfig); the framebuffer starts at +0x200000
 * (memorymap.h FRAMEBUFFER_ADDRESS) and the AmigaOS driver uses 46 MB of it
 * (z3660-drivers/rtg/gfx.c).  With autoconfig_rtg YES the combo is a Zorro
 * III board, manufacturer 0x144b product 1, above the identity map.
 */
#define ZZ9K_Z3660_PRODUCT_PC   0x144b0001UL
#define ZZ9K_Z3660_BASE         0x10000000UL
#define ZZ9K_Z3660_WINDOW       0x08000000UL
#define ZZ9K_Z3660_REG_SIZE     0x00001000UL  /* GETREG/SETREG bound; the page is not in the device space */
#define ZZ9K_Z3660_FB_PHYS      0x00200000UL
#define ZZ9K_Z3660_FB_MAX       0x02e00000UL
#define ZZ9K_Z3660_FW_MIN       0x0103
#define ZZ9K_Z3660_FW_MAJOR     1

#define ZZ9K_Z3660_REG_MODE        0x100
#define ZZ9K_Z3660_REG_CONFIG      0x104
#define ZZ9K_Z3660_REG_X1          0x110
#define ZZ9K_Z3660_REG_Y1          0x114
#define ZZ9K_Z3660_REG_X2          0x118
#define ZZ9K_Z3660_REG_Y2          0x11c
#define ZZ9K_Z3660_REG_PAN         0x120
#define ZZ9K_Z3660_REG_ROW_PITCH   0x124
#define ZZ9K_Z3660_REG_X3          0x128
#define ZZ9K_Z3660_REG_Y3          0x12c
#define ZZ9K_Z3660_REG_RGB         0x130
#define ZZ9K_Z3660_REG_FILLRECT    0x134
#define ZZ9K_Z3660_REG_COPYRECT    0x138
#define ZZ9K_Z3660_REG_BLIT_SRC    0x140
#define ZZ9K_Z3660_REG_BLIT_DST    0x144
#define ZZ9K_Z3660_REG_COLORMODE   0x148
#define ZZ9K_Z3660_REG_SRC_PITCH   0x14c
#define ZZ9K_Z3660_REG_VBLANK      0x17c
#define ZZ9K_Z3660_REG_SPLIT_POS   0x188
#define ZZ9K_Z3660_REG_FW_VERSION  0x1a0
#define ZZ9K_Z3660_REG_MONITOR_SWITCH 0x318  /* firmware `monitor_switch`, read-only here */
#define ZZ9K_Z3660_REG_FW_BETA     0x280
#define ZZ9K_Z3660_REG_FW_ALFA     0x284

/* Z3660 row console geometry: 1920x1080 (id 5) unless a build asks for 1280x800 (id 17) */
#ifdef ZZ9K_Z3660_CON_1280x800
#define ZZ9K_Z3660_CON_W        1280
#define ZZ9K_Z3660_CON_H        800
#else
#define ZZ9K_Z3660_CON_W        1920
#define ZZ9K_Z3660_CON_H        1080
#endif

/* zz_video_modes.h: ids 0-11 as the ZZ9000's, 12-15 differ, 17 is 1280x800 */
#define ZZ9K_Z3660_MODE_640x400    16
#define ZZ9K_Z3660_MODE_1280x800   17
#define ZZ9K_Z3660_MODE_1920x1200  18
#define ZZ9K_Z3660_MODE_1600x900   19
#define ZZ9K_Z3660_MODE_1680x1050  20
#define ZZ9K_Z3660_MODE_1366x768   21

/*
 * Kernel-side board table.  Not part of the ioctl ABI above.
 *
 * One row per supported board identity/window.  The ZZ9000 rows reproduce
 * the original driver; a target build (-DZZ9K_TARGET_...) adds rows whose
 * register file, window layout and mode table differ.  Register accesses go
 * through logical register ids so that a row can map, widen or omit a
 * register (ZZ9K_NOREG) without touching the code that uses it.
 */

#define ZZ9K_R_HW_VERSION      0
#define ZZ9K_R_MODE            1
#define ZZ9K_R_PAN             2   /* 32-bit; a 16-bit row holds HI at off, LO at off + 2 */
#define ZZ9K_R_X1              3
#define ZZ9K_R_Y1              4
#define ZZ9K_R_X2              5
#define ZZ9K_R_Y2              6
#define ZZ9K_R_ROW_PITCH       7
#define ZZ9K_R_X3              8
#define ZZ9K_R_Y3              9
#define ZZ9K_R_RGB             10  /* 32-bit, as ZZ9K_R_PAN */
#define ZZ9K_R_FILLRECT        11
#define ZZ9K_R_COPYRECT        12
#define ZZ9K_R_SRC             13  /* 32-bit, as ZZ9K_R_PAN */
#define ZZ9K_R_DST             14  /* 32-bit, as ZZ9K_R_PAN */
#define ZZ9K_R_COLORMODE       15
#define ZZ9K_R_SPLIT_POS       16
#define ZZ9K_R_FW_VERSION      17
#define ZZ9K_R_CAPTURE         18
#define ZZ9K_R_MONSW           19  /* the firmware's monitor_switch value; absent on the ZZ9000 */
#define ZZ9K_NREG              20
#define ZZ9K_NOREG             0xffff

#define ZZ9K_BT_REJECT         0x0001  /* identify, log the advice, refuse */
#define ZZ9K_BT_AGAGATE        0x0002  /* fixed row: probe only on an AGA chipset */
#define ZZ9K_BT_NOCAPTURE      0x0004  /* no native-video capture path: SETSWITCH is a no-op */
#define ZZ9K_BT_BLITTER        0x0008  /* FILLRECT/COPYRECT/BLITS use the card's blitter */

struct zz9k_modetab {
    unsigned short width;
    unsigned short height;
    unsigned short id;
};

struct zz9k_boardtab {
    char *name;                   /* as printed by the driver */
    char *advice;                 /* printed after a ZZ9K_BT_REJECT match */
    unsigned long autocon_pc;     /* (manufacturer << 16) | product; 0 = fixed row */
    unsigned long fixed_base;     /* fixed row: physical base */
    unsigned long fixed_size;     /* fixed row: window size */
    unsigned long fb_offset;      /* device-space offset of the framebuffer (GETINFO/GETFBINFO) */
    unsigned long fb_phys;        /* physical offset of the framebuffer inside the window */
    unsigned long fb_max;         /* framebuffer bytes; 0 = window - fb_offset - reserved_tail */
    unsigned long reserved_tail;  /* bytes at the end of the window not usable as framebuffer */
    unsigned long reg_size;       /* GETREG/SETREG bound */
    unsigned short regw;          /* register access width: 2 or 4 */
    unsigned short *regoff;       /* [ZZ9K_NREG] offsets, ZZ9K_NOREG = absent */
    struct zz9k_modetab *modes;   /* width/height -> id, terminated by width 0 */
    unsigned short con_width;     /* framebuffer console geometry (must be in modes) */
    unsigned short con_height;
    unsigned short fw_min;        /* warn below this firmware version; 0 = no floor */
    unsigned short fw_major;      /* presence test: FW_VERSION >> 8 must equal this; 0 = no test */
    unsigned short zorro;         /* GETINFO.zorro */
    unsigned short product;       /* GETINFO.product */
    unsigned short flags;         /* ZZ9K_BT_* */
};

#endif /* ZZ9000_H */
