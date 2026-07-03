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

struct zz9k_reg {
    unsigned short offset;
    unsigned short value;
};

#endif /* ZZ9000_H */
