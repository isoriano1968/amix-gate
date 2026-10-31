/*
 * zz9000.c - MNT ZZ9000 framebuffer/mode driver for Amiga UNIX.
 *
 * This is an AMIX Zorro II driver.  AMIX has no Zorro III support,
 * therefore the driver only probes the ZZ9000 Zorro II product id
 * (manufacturer 0x6d6e, product 3).  Zorro III and DMA paths from the AmigaOS
 * and NetBSD drivers are intentionally not used here.
 */

#include "sys/types.h"
#include "sys/immu.h"
#include "sys/param.h"
#include "sys/uio.h"
#include "sys/sysmacros.h"
#include "sys/inline.h"
#include "sys/errno.h"
#include "sys/systm.h"
#include "../../console/screen.h"
#include "../../console/console.h"
#include "zz9000.h"

#define ZZ9K_MAXBOARDS 1

#define ZZ9K_REG(base, off) \
    (*((volatile unsigned short *)((char *)(base) + (off))))

struct cred;

struct zz9k_configdev {
    struct zz9k_configdev *ln_succ;
    struct zz9k_configdev *ln_pred;
    unsigned char ln_type;
    char ln_pri;
    unsigned short ln_name[2];
    unsigned char cd_flags;
    unsigned char cd_pad;
    unsigned char er_type;
    unsigned char er_product;
    unsigned char er_flags;
    unsigned char er_reserved03;
    unsigned short er_manufacturer;
    unsigned short er_serialnumber[2];
    unsigned short er_initdiagvec;
    unsigned char er_reserved0c;
    unsigned char er_reserved0d;
    unsigned char er_reserved0e;
    unsigned char er_reserved0f;
    unsigned long cd_boardaddr;
    unsigned long cd_boardsize;
    unsigned short cd_slotaddr;
    unsigned short cd_slotsize;
    unsigned long cd_driver;
    struct zz9k_configdev *cd_nextcd;
    unsigned long cd_unused[4];
};

struct zz9k_bootinfo {
    struct zz9k_configdev autocon[ZZ9K_MAX_AUTOCON];
};

struct zz9k_charent {
    unsigned char ce_height;
    unsigned char ce_width;
    unsigned char ce_basel;
    unsigned char ce_move;
    unsigned char ce_bpl;
    unsigned char ce_pad;
    unsigned short ce_data;
};

struct zz9k_font {
    unsigned short f_magic;
    unsigned short f_flags;
    unsigned short f_count;
    unsigned short f_pad0;
    unsigned long f_length;
    unsigned char f_height;
    unsigned char f_width;
    unsigned char f_baseline;
    unsigned char f_pad1;
    struct zz9k_charent f_ctable[256];
};

extern struct zz9k_bootinfo bootinfo;
extern struct zz9k_font *builtinfont[];

struct zz9k_board {
    long base;
    unsigned long board_size;
    unsigned long fb_size;
    unsigned short hw_version;
    unsigned short fw_version;
    unsigned short mode;
    unsigned short width;
    unsigned short height;
    unsigned short color_mode;
    unsigned short open_count;
};

static struct zz9k_board zz9k_boards[ZZ9K_MAXBOARDS];

static unsigned short zz9k_mode_id();
static unsigned short zz9k_bpp();
static int zz9k_probe();
static int zz9k_present();
static int zz9k_attach();
static void zz9k_fill_autocon();
static void zz9k_write32();
static void zz9k_blit_fill();
static void zz9k_blit_copy();
static void zz9k_cpu_fillrect();
static void zz9k_cpu_copyrect();
static void zz9k_fill_fbinfo();
static void zz9k_set_mode_hw();
static void zz9k_set_switch_hw();
static int zz9k_console_enable();
static int zz9k_console_redirect_enable();
static void zz9k_console_disable();
static int zz9k_console_test();
static void zz9k_console_puts();
static void zz9k_console_fill_rect();
static void zz9k_conputc();
static unsigned short zz9k_console_color();

extern void (*conputc)();

static void (*zz9k_old_conputc)();
static int zz9k_console_on;
static int zz9k_console_redirect;
static int zz9k_console_disabled;
static int zz9k_console_row;
static int zz9k_console_col;
static int zz9k_console_cols;
static int zz9k_console_rows;
static int zz9k_console_font_w;
static int zz9k_console_font_h;
static int zz9k_console_cursor_on;
static int zz9k_console_esc;
static int zz9k_console_argc;
static int zz9k_console_arg;
static int zz9k_console_args[4];
static unsigned short zz9k_console_fg;
static unsigned short zz9k_console_bg;
static struct zz9k_board *zz9k_console_bp;
static struct zz9k_font *zz9k_console_font;

#define ZZ9K_CON_WIDTH  1920
#define ZZ9K_CON_HEIGHT 800
#define ZZ9K_CON_MODE   ZZ9K_MODE_1920x800
#define ZZ9K_CON_COLOR  ZZ9K_COLOR_16BIT565
#define ZZ9K_FALLBACK_FONT_W 6
#define ZZ9K_FALLBACK_FONT_H 8
#define ZZ9K_FG         0xffff
#define ZZ9K_BG         0x0000
#define ZZ9K_FONT_MAGIC 0x2a46
#define ZZ9K_ESC_NONE   0
#define ZZ9K_ESC_ESC    1
#define ZZ9K_ESC_CSI    2

static unsigned short
zz9k_mode_id(width, height)
unsigned short width;
unsigned short height;
{
    if (width == 1280 && height == 720) return ZZ9K_MODE_1280x720;
    if (width == 800 && height == 600) return ZZ9K_MODE_800x600;
    if (width == 640 && height == 480) return ZZ9K_MODE_640x480;
    if (width == 1024 && height == 768) return ZZ9K_MODE_1024x768;
    if (width == 1280 && height == 1024) return ZZ9K_MODE_1280x1024;
    if (width == 1920 && height == 1080) return ZZ9K_MODE_1920x1080;
    if (width == 720 && height == 576) return ZZ9K_MODE_720x576;
    if (width == 720 && height == 480) return ZZ9K_MODE_720x480;
    if (width == 640 && height == 512) return ZZ9K_MODE_640x512;
    if (width == 1600 && height == 1200) return ZZ9K_MODE_1600x1200;
    if (width == 2560 && height == 1440) return ZZ9K_MODE_2560x1440;
    if (width == 640 && height == 400) return ZZ9K_MODE_640x400;
    if (width == 1920 && height == 800) return ZZ9K_MODE_1920x800;
    return 0xffff;
}

static unsigned short
zz9k_bpp(color_mode)
unsigned short color_mode;
{
    switch (color_mode) {
    case ZZ9K_COLOR_8BIT:
        return 1;
    case ZZ9K_COLOR_15BIT:
    case ZZ9K_COLOR_16BIT565:
        return 2;
    case ZZ9K_COLOR_32BIT:
        return 4;
    default:
        return 0;
    }
}

static void
zz9k_fill_fbinfo(bp, fbi)
struct zz9k_board *bp;
struct zz9k_fbinfo *fbi;
{
    fbi->fb_offset = ZZ9K_FB_OFFSET;
    fbi->fb_size = bp->fb_size;
    fbi->width = bp->width;
    fbi->height = bp->height;
    fbi->color_mode = bp->color_mode;
    fbi->bytes_per_pixel = zz9k_bpp(bp->color_mode);
    fbi->stride = bp->width * fbi->bytes_per_pixel;
    fbi->red_mask = 0;
    fbi->green_mask = 0;
    fbi->blue_mask = 0;
    fbi->alpha_mask = 0;
    fbi->red_shift = 0;
    fbi->green_shift = 0;
    fbi->blue_shift = 0;
    fbi->alpha_shift = 0;

    switch (bp->color_mode) {
    case ZZ9K_COLOR_8BIT:
        fbi->depth = 8;
        break;
    case ZZ9K_COLOR_15BIT:
        fbi->depth = 15;
        fbi->red_mask = 0x7c00;
        fbi->green_mask = 0x03e0;
        fbi->blue_mask = 0x001f;
        fbi->alpha_mask = 0x8000;
        fbi->red_shift = 10;
        fbi->green_shift = 5;
        fbi->blue_shift = 0;
        fbi->alpha_shift = 15;
        break;
    case ZZ9K_COLOR_16BIT565:
        fbi->depth = 16;
        fbi->red_mask = 0xf800;
        fbi->green_mask = 0x07e0;
        fbi->blue_mask = 0x001f;
        fbi->red_shift = 11;
        fbi->green_shift = 5;
        fbi->blue_shift = 0;
        break;
    case ZZ9K_COLOR_32BIT:
        fbi->depth = 32;
        fbi->red_mask = 0x00ff0000;
        fbi->green_mask = 0x0000ff00;
        fbi->blue_mask = 0x000000ff;
        fbi->alpha_mask = 0xff000000;
        fbi->red_shift = 16;
        fbi->green_shift = 8;
        fbi->blue_shift = 0;
        fbi->alpha_shift = 24;
        break;
    default:
        fbi->depth = 0;
        break;
    }
}

static int
zz9k_present(base)
long base;
{
    unsigned short hw, fw;

    if (!base)
        return 0;

    hw = ZZ9K_REG(base, ZZ9K_REG_HW_VERSION);
    fw = ZZ9K_REG(base, ZZ9K_REG_FW_VERSION);
    if ((hw == 0 || hw == 0xffff) && (fw == 0 || fw == 0xffff))
        return 0;
    return 1;
}


static int
zz9k_attach(bp, base, size)
struct zz9k_board *bp;
long base;
long size;
{
    if (!zz9k_present(base)) {
        printf("zz9000: autocon base 0x%x did not look like ZZ9000 regs\n",
            (unsigned int)base);
        return 0;
    }

    bp->base = base;
    bp->board_size = (unsigned long)size;
    if (bp->board_size > ZZ9K_FB_OFFSET + ZZ9K_Z2_RESERVED_TAIL)
        bp->fb_size = bp->board_size - ZZ9K_FB_OFFSET - ZZ9K_Z2_RESERVED_TAIL;
    else
        bp->fb_size = 0;
    bp->hw_version = ZZ9K_REG(base, ZZ9K_REG_HW_VERSION);
    bp->fw_version = ZZ9K_REG(base, ZZ9K_REG_FW_VERSION);
    bp->width = 800;
    bp->height = 600;
    bp->color_mode = ZZ9K_COLOR_16BIT565;
    bp->mode = ZZ9K_MODE_800x600;

    printf("zz9000: Zorro II board at 0x%x size 0x%x hw 0x%x fw 0x%x\n",
        (unsigned int)bp->base, (unsigned int)bp->board_size,
        bp->hw_version, bp->fw_version);
    if (bp->fw_version < ZZ9K_FW_VER_MIN)
        printf("zz9000: warning firmware 0x%x is older than 0x%x\n",
            bp->fw_version, ZZ9K_FW_VER_MIN);
    return 1;
}

static void
zz9k_fill_autocon(dump)
struct zz9k_autocon_dump *dump;
{
    int i;
    struct zz9k_configdev *cd;

    dump->count = ZZ9K_MAX_AUTOCON;
    for (i = 0; i < ZZ9K_MAX_AUTOCON; ++i) {
        cd = &bootinfo.autocon[i];
        dump->entry[i].manufacturer = cd->er_manufacturer;
        dump->entry[i].product = cd->er_product;
        dump->entry[i].type = cd->er_type;
        dump->entry[i].flags = cd->er_flags;
        dump->entry[i].board_addr = cd->cd_boardaddr;
        dump->entry[i].board_size = cd->cd_boardsize;
        dump->entry[i].slot_addr = cd->cd_slotaddr;
        dump->entry[i].slot_size = cd->cd_slotsize;
    }
}
static int
zz9k_probe(unit)
int unit;
{
    long base, size;
    struct zz9k_board *bp;

    if (unit >= ZZ9K_MAXBOARDS)
        return 0;

    bp = &zz9k_boards[unit];
    if (bp->base)
        return 1;

    if (autocon(ZZ9K_PRODUCT_PC_Z2, unit, &base, &size)) {
        printf("zz9000: autocon match pc 0x%x base 0x%x size 0x%x\n",
            (unsigned int)ZZ9K_PRODUCT_PC_Z2, (unsigned int)base,
            (unsigned int)size);
        return zz9k_attach(bp, base, size);
    }

    if (autocon(ZZ9K_PRODUCT_PC_Z3, unit, &base, &size)) {
        printf("zz9000: Zorro III product at 0x%x size 0x%x unsupported\n",
            (unsigned int)base, (unsigned int)size);
        printf("zz9000: configure card as Zorro II product 3 for AMIX\n");
        return 0;
    }

    printf("zz9000: autocon did not find manufacturer 0x%x product %d\n",
        ZZ9K_MNT_MANUFACTURER, ZZ9K_PRODUCT_Z2);
    return 0;
}

static void
zz9k_write32(base, off, value)
long base;
unsigned short off;
unsigned long value;
{
    ZZ9K_REG(base, off) = (unsigned short)(value >> 16);
    ZZ9K_REG(base, off + 2) = (unsigned short)(value & 0xffff);
}

static void
zz9k_blit_fill(bp, x, y, w, h, color)
struct zz9k_board *bp;
int x, y, w, h;
unsigned short color;
{
    if (!bp || !bp->base || w <= 0 || h <= 0)
        return;

    zz9k_write32(bp->base, ZZ9K_REG_BLITTER_DST_HI, 0);
    zz9k_write32(bp->base, ZZ9K_REG_BLITTER_RGB_HI, (unsigned long)color);
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_ROW_PITCH) =
        (unsigned short)((bp->width * zz9k_bpp(bp->color_mode)) >> 2);
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_COLORMODE) = bp->color_mode;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_X1) = (unsigned short)x;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_Y1) = (unsigned short)y;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_X2) = (unsigned short)w;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_Y2) = (unsigned short)h;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_OP_FILLRECT) = 0xff;
}

static void
zz9k_blit_copy(bp, sx, sy, dx, dy, w, h)
struct zz9k_board *bp;
int sx, sy, dx, dy, w, h;
{
    if (!bp || !bp->base || w <= 0 || h <= 0)
        return;

    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_Y1) = (unsigned short)dy;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_Y2) = (unsigned short)h;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_Y3) = (unsigned short)sy;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_X1) = (unsigned short)dx;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_X2) = (unsigned short)w;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_X3) = (unsigned short)sx;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_ROW_PITCH) =
        (unsigned short)((bp->width * zz9k_bpp(bp->color_mode)) >> 2);
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_COLORMODE) =
        (unsigned short)(bp->color_mode | (0xff << 8));
    zz9k_write32(bp->base, ZZ9K_REG_BLITTER_SRC_HI, 0);
    zz9k_write32(bp->base, ZZ9K_REG_BLITTER_DST_HI, 0);
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_OP_COPYRECT) = 1;
}

static void
zz9k_cpu_fillrect(bp, x, y, w, h, color)
struct zz9k_board *bp;
int x, y, w, h;
unsigned short color;
{
    unsigned short *fb;
    unsigned short *p;
    int xx, yy;

    if (!bp || !bp->base || w <= 0 || h <= 0)
        return;

    fb = (unsigned short *)((char *)bp->base + ZZ9K_FB_OFFSET);
    for (yy = y; yy < y + h; ++yy) {
        p = fb + ((unsigned long)yy * bp->width) + x;
        for (xx = 0; xx < w; ++xx)
            *p++ = color;
    }
}

static void
zz9k_cpu_copyrect(bp, sx, sy, dx, dy, w, h)
struct zz9k_board *bp;
int sx, sy, dx, dy, w, h;
{
    unsigned short *fb;
    unsigned short *src;
    unsigned short *dst;
    int row, col;
    int row_start, row_end, row_step;
    int col_start, col_end, col_step;

    if (!bp || !bp->base || w <= 0 || h <= 0)
        return;

    fb = (unsigned short *)((char *)bp->base + ZZ9K_FB_OFFSET);

    if (dy > sy) {
        row_start = h - 1;
        row_end = -1;
        row_step = -1;
    } else {
        row_start = 0;
        row_end = h;
        row_step = 1;
    }

    if (dy == sy && dx > sx) {
        col_start = w - 1;
        col_end = -1;
        col_step = -1;
    } else {
        col_start = 0;
        col_end = w;
        col_step = 1;
    }

    for (row = row_start; row != row_end; row += row_step) {
        src = fb + ((unsigned long)(sy + row) * bp->width) + sx;
        dst = fb + ((unsigned long)(dy + row) * bp->width) + dx;
        for (col = col_start; col != col_end; col += col_step)
            dst[col] = src[col];
    }
}

static void
zz9k_set_mode_hw(bp, mode, color_mode, scale_mode)
struct zz9k_board *bp;
unsigned short mode;
unsigned short color_mode;
unsigned short scale_mode;
{
    ZZ9K_REG(bp->base, ZZ9K_REG_CX_CAPTURE) = 0;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_X1) = 0;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_Y1) = 0;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_X2) = 0;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_Y2) = 0;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_COLORMODE) = color_mode;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_SRC_HI) = 0;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_SRC_LO) = 0;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_DST_HI) = 0;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_DST_LO) = 0;
    ZZ9K_REG(bp->base, ZZ9K_REG_BLITTER_SPLIT_POS) = 0;
    zz9k_write32(bp->base, ZZ9K_REG_PAN_HI, 0);
    ZZ9K_REG(bp->base, ZZ9K_REG_MODE) =
        (unsigned short)(mode | (color_mode << 8) | (scale_mode << 12));
    bp->mode = mode;
    bp->color_mode = color_mode;
}

static void
zz9k_set_switch_hw(bp, enabled)
struct zz9k_board *bp;
unsigned short enabled;
{
    if (enabled == ZZ9K_SWITCH_CAPTURE) {
        zz9k_write32(bp->base, ZZ9K_REG_PAN_HI, ZZ9K_CAPTURE_PAN);
        ZZ9K_REG(bp->base, ZZ9K_REG_CX_CAPTURE) = 1;
    } else {
        ZZ9K_REG(bp->base, ZZ9K_REG_CX_CAPTURE) = 0;
        zz9k_write32(bp->base, ZZ9K_REG_PAN_HI, 0);
    }
}

static unsigned char
zz9k_glyph(c, row)
unsigned char c;
int row;
{
    static unsigned char space[7] = {0,0,0,0,0,0,0};
    static unsigned char quest[7] = {0x0e,0x11,0x01,0x02,0x04,0x00,0x04};
    static unsigned char dash[7] = {0,0,0,0x1f,0,0,0};
    static unsigned char dot[7] = {0,0,0,0,0,0x0c,0x0c};
    static unsigned char colon[7] = {0,0x0c,0x0c,0,0x0c,0x0c,0};
    static unsigned char slash[7] = {0x01,0x02,0x04,0x08,0x10,0,0};
    static unsigned char digits[10][7] = {
        {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e},
        {0x04,0x0c,0x04,0x04,0x04,0x04,0x0e},
        {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f},
        {0x1f,0x02,0x04,0x02,0x01,0x11,0x0e},
        {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02},
        {0x1f,0x10,0x1e,0x01,0x01,0x11,0x0e},
        {0x06,0x08,0x10,0x1e,0x11,0x11,0x0e},
        {0x1f,0x01,0x02,0x04,0x08,0x08,0x08},
        {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e},
        {0x0e,0x11,0x11,0x0f,0x01,0x02,0x0c}
    };
    static unsigned char letters[26][7] = {
        {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11},
        {0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e},
        {0x0e,0x11,0x10,0x10,0x10,0x11,0x0e},
        {0x1e,0x11,0x11,0x11,0x11,0x11,0x1e},
        {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f},
        {0x1f,0x10,0x10,0x1e,0x10,0x10,0x10},
        {0x0e,0x11,0x10,0x17,0x11,0x11,0x0f},
        {0x11,0x11,0x11,0x1f,0x11,0x11,0x11},
        {0x0e,0x04,0x04,0x04,0x04,0x04,0x0e},
        {0x07,0x02,0x02,0x02,0x12,0x12,0x0c},
        {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
        {0x10,0x10,0x10,0x10,0x10,0x10,0x1f},
        {0x11,0x1b,0x15,0x15,0x11,0x11,0x11},
        {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
        {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e},
        {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10},
        {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d},
        {0x1e,0x11,0x11,0x1e,0x14,0x12,0x11},
        {0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e},
        {0x1f,0x04,0x04,0x04,0x04,0x04,0x04},
        {0x11,0x11,0x11,0x11,0x11,0x11,0x0e},
        {0x11,0x11,0x11,0x11,0x11,0x0a,0x04},
        {0x11,0x11,0x11,0x15,0x15,0x15,0x0a},
        {0x11,0x11,0x0a,0x04,0x0a,0x11,0x11},
        {0x11,0x11,0x0a,0x04,0x04,0x04,0x04},
        {0x1f,0x01,0x02,0x04,0x08,0x10,0x1f}
    };

    if (row <= 0 || row >= ZZ9K_FALLBACK_FONT_H)
        return 0;
    --row;
    if (c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
    if (c >= 'A' && c <= 'Z')
        return letters[c - 'A'][row];
    if (c >= '0' && c <= '9')
        return digits[c - '0'][row];
    if (c == ' ') return space[row];
    if (c == '-') return dash[row];
    if (c == '.') return dot[row];
    if (c == ':') return colon[row];
    if (c == '/' || c == '\\') return slash[row];
    return quest[row];
}

static void
zz9k_console_clear_line(y)
int y;
{
    zz9k_console_fill_rect(0, y, ZZ9K_CON_WIDTH, zz9k_console_font_h,
        zz9k_console_bg);
}

static void
zz9k_console_clear_eol()
{
    int y, px;

    px = zz9k_console_col * zz9k_console_font_w;
    y = zz9k_console_row * zz9k_console_font_h;
    zz9k_console_fill_rect(px, y, ZZ9K_CON_WIDTH - px,
        zz9k_console_font_h, zz9k_console_bg);
}

static void
zz9k_console_clear()
{
    zz9k_console_fill_rect(0, 0, ZZ9K_CON_WIDTH, ZZ9K_CON_HEIGHT,
        zz9k_console_bg);
}

static void
zz9k_console_fill_rect(x, y, w, h, color)
int x, y, w, h;
unsigned short color;
{
    unsigned short *p;
    int px, py;

    if (!zz9k_console_bp || !zz9k_console_bp->base)
        return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= ZZ9K_CON_WIDTH || y >= ZZ9K_CON_HEIGHT || w <= 0 || h <= 0)
        return;
    if (x + w > ZZ9K_CON_WIDTH)
        w = ZZ9K_CON_WIDTH - x;
    if (y + h > ZZ9K_CON_HEIGHT)
        h = ZZ9K_CON_HEIGHT - y;

    for (py = 0; py < h; ++py) {
        p = (unsigned short *)((char *)zz9k_console_bp->base +
            ZZ9K_FB_OFFSET + ((unsigned long)(y + py) * ZZ9K_CON_WIDTH +
            x) * 2);
        for (px = 0; px < w; ++px)
            *p++ = color;
    }
}

static void
zz9k_console_scroll()
{
    unsigned short *dst, *src;
    unsigned long n;

    dst = (unsigned short *)((char *)zz9k_console_bp->base + ZZ9K_FB_OFFSET);
    src = dst + (ZZ9K_CON_WIDTH * zz9k_console_font_h);
    n = (unsigned long)ZZ9K_CON_WIDTH *
        (ZZ9K_CON_HEIGHT - zz9k_console_font_h);
    while (n--)
        *dst++ = *src++;
    zz9k_console_clear_line(ZZ9K_CON_HEIGHT - zz9k_console_font_h);
}

static void
zz9k_console_draw(c)
unsigned char c;
{
    unsigned short *p;
    unsigned char bits;
    unsigned char *data;
    unsigned char *rowdata;
    struct zz9k_charent *ce;
    unsigned short rowbits;
    int px, py, x, y;
    int on;

    x = zz9k_console_col * zz9k_console_font_w;
    y = zz9k_console_row * zz9k_console_font_h;
    ce = (struct zz9k_charent *)0;
    data = (unsigned char *)0;
    if (zz9k_console_font &&
        zz9k_console_font->f_magic == ZZ9K_FONT_MAGIC) {
        ce = &zz9k_console_font->f_ctable[c];
        if (ce->ce_data == 0 && c != ' ')
            ce = &zz9k_console_font->f_ctable['?'];
        if (ce->ce_data)
            data = (unsigned char *)zz9k_console_font + ce->ce_data;
    }

    for (py = 0; py < zz9k_console_font_h; ++py) {
        bits = zz9k_glyph(c, py);
        p = (unsigned short *)((char *)zz9k_console_bp->base +
            ZZ9K_FB_OFFSET + ((unsigned long)(y + py) * ZZ9K_CON_WIDTH +
            x) * 2);
        for (px = 0; px < zz9k_console_font_w; ++px) {
            on = 0;
            if (data && py < ce->ce_height && px < ce->ce_width) {
                if (ce->ce_bpl == 2) {
                    rowdata = (unsigned char *)zz9k_console_font +
                        ce->ce_data + (py * ce->ce_bpl);
                    rowbits = ((unsigned short)rowdata[0] << 8) |
                        rowdata[1];
                    on = rowbits & (1 << (ce->ce_width - 1 - px));
                } else {
                    bits = data[(py * ce->ce_bpl) + (px >> 3)];
                    on = bits & (0x80 >> (px & 7));
                }
            } else if (!data && py < ZZ9K_FALLBACK_FONT_H &&
                px < ZZ9K_FALLBACK_FONT_W) {
                on = bits & (0x10 >> px);
            }
            *p++ = on ? zz9k_console_fg : zz9k_console_bg;
        }
    }
}

static void
zz9k_console_cursor(on)
int on;
{
    unsigned short *p;
    int px, py, x, y;

    if (!zz9k_console_bp)
        return;
    if (on == zz9k_console_cursor_on)
        return;

    x = zz9k_console_col * zz9k_console_font_w;
    y = zz9k_console_row * zz9k_console_font_h;
    for (py = 0; py < zz9k_console_font_h; ++py) {
        p = (unsigned short *)((char *)zz9k_console_bp->base +
            ZZ9K_FB_OFFSET + ((unsigned long)(y + py) * ZZ9K_CON_WIDTH +
            x) * 2);
        for (px = 0; px < zz9k_console_font_w; ++px) {
            *p = *p ^ 0xffff;
            ++p;
        }
    }
    zz9k_console_cursor_on = on;
}

static unsigned short
zz9k_console_color(n)
int n;
{
    static unsigned short colors[8] = {
        0x0000, 0xf800, 0x07e0, 0xffe0,
        0x001f, 0xf81f, 0x07ff, 0xffff
    };

    if (n < 0 || n > 7)
        return ZZ9K_FG;
    return colors[n];
}

static void
zz9k_console_reset_attr()
{
    zz9k_console_fg = ZZ9K_FG;
    zz9k_console_bg = ZZ9K_BG;
}

static void
zz9k_console_newline()
{
    zz9k_console_col = 0;
    if (++zz9k_console_row >= zz9k_console_rows) {
        zz9k_console_scroll();
        zz9k_console_row = zz9k_console_rows - 1;
    }
}

static void
zz9k_console_move(row, col)
int row;
int col;
{
    if (row < 0)
        row = 0;
    if (col < 0)
        col = 0;
    if (row >= zz9k_console_rows)
        row = zz9k_console_rows - 1;
    if (col >= zz9k_console_cols)
        col = zz9k_console_cols - 1;
    zz9k_console_row = row;
    zz9k_console_col = col;
}

static int
zz9k_console_argn(n, def)
int n;
int def;
{
    if (n < 0 || n > zz9k_console_argc)
        return def;
    if (zz9k_console_args[n] == 0)
        return def;
    return zz9k_console_args[n];
}

static void
zz9k_console_sgr()
{
    int i, a;
    unsigned short tmp;

    if (zz9k_console_argc == 0 && zz9k_console_args[0] == 0) {
        zz9k_console_reset_attr();
        return;
    }
    for (i = 0; i <= zz9k_console_argc; ++i) {
        a = zz9k_console_args[i];
        if (a == 0)
            zz9k_console_reset_attr();
        else if (a == 1)
            zz9k_console_fg = 0xffff;
        else if (a == 7) {
            tmp = zz9k_console_fg;
            zz9k_console_fg = zz9k_console_bg;
            zz9k_console_bg = tmp;
        } else if (a >= 30 && a <= 37)
            zz9k_console_fg = zz9k_console_color(a - 30);
        else if (a >= 40 && a <= 47)
            zz9k_console_bg = zz9k_console_color(a - 40);
    }
}

static void
zz9k_console_csi(c)
unsigned char c;
{
    int n;

    if (c >= '0' && c <= '9') {
        zz9k_console_args[zz9k_console_arg] =
            zz9k_console_args[zz9k_console_arg] * 10 + c - '0';
        return;
    }
    if (c == ';') {
        if (zz9k_console_arg < 3)
            ++zz9k_console_arg;
        if (zz9k_console_arg > zz9k_console_argc)
            zz9k_console_argc = zz9k_console_arg;
        return;
    }
    if (zz9k_console_arg > zz9k_console_argc)
        zz9k_console_argc = zz9k_console_arg;
    n = zz9k_console_argn(0, 1);

    switch (c) {
    case 'A':
        zz9k_console_move(zz9k_console_row - n, zz9k_console_col);
        break;
    case 'B':
        zz9k_console_move(zz9k_console_row + n, zz9k_console_col);
        break;
    case 'C':
        zz9k_console_move(zz9k_console_row, zz9k_console_col + n);
        break;
    case 'D':
        zz9k_console_move(zz9k_console_row, zz9k_console_col - n);
        break;
    case 'H':
    case 'f':
        zz9k_console_move(zz9k_console_argn(0, 1) - 1,
            zz9k_console_argn(1, 1) - 1);
        break;
    case 'J':
        if (zz9k_console_args[0] == 2) {
            zz9k_console_clear();
            zz9k_console_move(0, 0);
        } else if (zz9k_console_args[0] == 1) {
            zz9k_console_fill_rect(0, 0, ZZ9K_CON_WIDTH,
                zz9k_console_row * zz9k_console_font_h, zz9k_console_bg);
            zz9k_console_fill_rect(0,
                zz9k_console_row * zz9k_console_font_h,
                (zz9k_console_col + 1) * zz9k_console_font_w,
                zz9k_console_font_h, zz9k_console_bg);
        } else {
            zz9k_console_clear_eol();
            zz9k_console_fill_rect(0,
                (zz9k_console_row + 1) * zz9k_console_font_h,
                ZZ9K_CON_WIDTH,
                ZZ9K_CON_HEIGHT -
                ((zz9k_console_row + 1) * zz9k_console_font_h),
                zz9k_console_bg);
        }
        break;
    case 'K':
        zz9k_console_clear_eol();
        break;
    case 'm':
        zz9k_console_sgr();
        break;
    }

    zz9k_console_esc = ZZ9K_ESC_NONE;
    zz9k_console_arg = 0;
    zz9k_console_argc = 0;
    zz9k_console_args[0] = 0;
    zz9k_console_args[1] = 0;
    zz9k_console_args[2] = 0;
    zz9k_console_args[3] = 0;
}

static void
zz9k_conputc(c)
unsigned char c;
{
    if (!zz9k_console_on || !zz9k_console_bp)
        return;

    zz9k_console_cursor(0);

    if (zz9k_console_esc == ZZ9K_ESC_CSI) {
        zz9k_console_csi(c);
        zz9k_console_cursor(1);
        return;
    }
    if (zz9k_console_esc == ZZ9K_ESC_ESC) {
        if (c == '[') {
            zz9k_console_esc = ZZ9K_ESC_CSI;
            zz9k_console_arg = 0;
            zz9k_console_argc = 0;
            zz9k_console_args[0] = 0;
            zz9k_console_args[1] = 0;
            zz9k_console_args[2] = 0;
            zz9k_console_args[3] = 0;
        } else
            zz9k_console_esc = ZZ9K_ESC_NONE;
        zz9k_console_cursor(1);
        return;
    }
    if (c == 0x1b) {
        zz9k_console_esc = ZZ9K_ESC_ESC;
        zz9k_console_cursor(1);
        return;
    }

    if (c == '\r') {
        zz9k_console_col = 0;
        zz9k_console_cursor(1);
        return;
    }
    if (c == '\n') {
        zz9k_console_newline();
        zz9k_console_cursor(1);
        return;
    }
    if (c == '\t') {
        zz9k_console_col = (zz9k_console_col + 8) & ~7;
        if (zz9k_console_col >= zz9k_console_cols) {
            zz9k_console_newline();
        }
        zz9k_console_cursor(1);
        return;
    }
    if (c == '\b') {
        if (zz9k_console_col)
            --zz9k_console_col;
        zz9k_console_cursor(1);
        return;
    }
    zz9k_console_draw(c);
    if (++zz9k_console_col >= zz9k_console_cols) {
        zz9k_console_newline();
    }
    zz9k_console_cursor(1);
}

static int
zz9k_console_start(bp)
struct zz9k_board *bp;
{
    if (!bp || !bp->base)
        return ENXIO;
    if ((unsigned long)ZZ9K_CON_WIDTH * ZZ9K_CON_HEIGHT * 2 > bp->fb_size)
        return ENOMEM;

    zz9k_set_mode_hw(bp, ZZ9K_CON_MODE, ZZ9K_CON_COLOR, 0);
    bp->width = ZZ9K_CON_WIDTH;
    bp->height = ZZ9K_CON_HEIGHT;
    zz9k_set_switch_hw(bp, ZZ9K_SWITCH_RTG);

    zz9k_console_bp = bp;
    zz9k_console_font = builtinfont[0];
    if (!zz9k_console_font ||
        zz9k_console_font->f_magic != ZZ9K_FONT_MAGIC) {
        zz9k_console_font = (struct zz9k_font *)0;
        zz9k_console_font_w = ZZ9K_FALLBACK_FONT_W;
        zz9k_console_font_h = ZZ9K_FALLBACK_FONT_H;
    } else {
        zz9k_console_font_w =
            zz9k_console_font->f_ctable['M'].ce_move + 1;
        zz9k_console_font_h = zz9k_console_font->f_height;
    }
    zz9k_console_cols = ZZ9K_CON_WIDTH / zz9k_console_font_w;
    zz9k_console_rows = ZZ9K_CON_HEIGHT / zz9k_console_font_h;
    zz9k_console_col = 0;
    zz9k_console_row = 0;
    zz9k_console_cursor_on = 0;
    zz9k_console_esc = ZZ9K_ESC_NONE;
    zz9k_console_arg = 0;
    zz9k_console_argc = 0;
    zz9k_console_args[0] = 0;
    zz9k_console_args[1] = 0;
    zz9k_console_args[2] = 0;
    zz9k_console_args[3] = 0;
    zz9k_console_reset_attr();
    zz9k_console_clear();

    zz9k_console_on = 1;
    zz9k_console_cursor(1);
    return 0;
}

static int
zz9k_console_enable(bp)
struct zz9k_board *bp;
{
    int error;

    error = zz9k_console_start(bp);
    if (error)
        return error;
    if (!zz9k_old_conputc)
        zz9k_old_conputc = conputc;
    conputc = zz9k_conputc;
    zz9k_console_redirect = 1;
    zz9k_console_disabled = 0;
    return 0;
}

static int
zz9k_console_redirect_enable(bp)
struct zz9k_board *bp;
{
    int error;

    error = zz9k_console_start(bp);
    if (error)
        return error;
    if (!zz9k_old_conputc)
        zz9k_old_conputc = conputc;
    conputc = zz9k_conputc;
    zz9k_console_redirect = 1;
    zz9k_console_disabled = 0;
    return 0;
}

static void
zz9k_console_disable()
{
    struct zz9k_board *bp;

    bp = zz9k_console_bp;
    zz9k_console_cursor(0);
    if (zz9k_old_conputc)
        conputc = zz9k_old_conputc;
    zz9k_console_redirect = 0;
    zz9k_console_disabled = 1;
    zz9k_console_on = 0;
    if (bp)
        zz9k_set_switch_hw(bp, ZZ9K_SWITCH_CAPTURE);
}

int
zz9000consoleputc(c)
unsigned char c;
{
    if (!zz9k_console_redirect)
        return 0;
    if (zz9k_console_bp)
        zz9k_set_switch_hw(zz9k_console_bp, ZZ9K_SWITCH_RTG);
    zz9k_conputc(c);
    return 1;
}

int
zz9000consolewrite(p, n)
unsigned char *p;
unsigned n;
{
    if (!zz9k_console_redirect)
        return 0;
    if (zz9k_console_bp)
        zz9k_set_switch_hw(zz9k_console_bp, ZZ9K_SWITCH_RTG);
    while (n--) {
        zz9k_conputc(*p);
        ++p;
    }
    return 1;
}

static void
zz9k_console_puts(s)
char *s;
{
    while (*s)
        zz9k_conputc(*s++);
}

static int
zz9k_console_test(bp)
struct zz9k_board *bp;
{
    int row;
    char digit;

    if (!zz9k_console_on) {
        if (zz9k_console_start(bp))
            return ENXIO;
    }
    zz9k_set_switch_hw(zz9k_console_bp, ZZ9K_SWITCH_RTG);

    zz9k_console_puts("\033[2J");
    zz9k_console_puts("ZZ9000 direct console renderer test\n");
    zz9k_console_puts("ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789\n");
    zz9k_console_puts("THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG\n");
    zz9k_console_puts("\033[31mRED\033[0m \033[32mGREEN\033[0m ");
    zz9k_console_puts("\033[33mYELLOW\033[0m \033[34mBLUE\033[0m ");
    zz9k_console_puts("\033[35mMAGENTA\033[0m \033[36mCYAN\033[0m\n");
    zz9k_console_puts("cursor movement test: start\033[2DOK\n");
    zz9k_console_puts("clear to eol test: this tail should disappear\033[19D\033[Kdone\n");
    zz9k_console_puts("\nscroll/check rows:\n");
    for (row = 0; row < zz9k_console_rows + 4; ++row) {
        zz9k_console_puts("row ");
        digit = '0' + ((row / 10) % 10);
        zz9k_conputc(digit);
        digit = '0' + (row % 10);
        zz9k_conputc(digit);
        zz9k_console_puts(" -- no garbage should remain after this line\n");
    }
    return 0;
}

void
zz9000init()
{
    (void)zz9k_probe(0);
}

int
zz9000open(devp, mode, type, cr)
dev_t *devp;
int mode;
int type;
struct cred *cr;
{
    int unit;

    unit = getminor(*devp);
    if (unit >= ZZ9K_MAXBOARDS)
        return ENXIO;
    ++zz9k_boards[unit].open_count;
    return 0;
}

int
zz9000close(dev, mode, type, cr)
dev_t dev;
int mode;
int type;
struct cred *cr;
{
    int unit;

    unit = getminor(dev);
    if (unit < ZZ9K_MAXBOARDS && zz9k_boards[unit].open_count)
        --zz9k_boards[unit].open_count;
    return 0;
}

static int
zz9000rw(dev, direction, uiop)
dev_t dev;
uio_rw_t direction;
struct uio *uiop;
{
    int unit, error;
    unsigned long count, end;
    struct zz9k_board *bp;

    unit = getminor(dev);
    if (!zz9k_probe(unit))
        return ENXIO;
    bp = &zz9k_boards[unit];

    count = uiop->uio_resid;
    if (count == 0)
        return 0;
    if (uiop->uio_offset < 0 ||
        (unsigned long)uiop->uio_offset >= bp->board_size)
        return ENXIO;

    end = (unsigned long)uiop->uio_offset + count;
    if (end > bp->board_size)
        count = bp->board_size - (unsigned long)uiop->uio_offset;
    if (direction == UIO_WRITE && count == 0)
        return ENXIO;

    error = uiomove((char *)bp->base + uiop->uio_offset,
        count, direction, uiop);
    return error;
}

int
zz9000read(dev, uiop, cr)
dev_t dev;
struct uio *uiop;
struct cred *cr;
{
    return zz9000rw(dev, UIO_READ, uiop);
}

int
zz9000write(dev, uiop, cr)
dev_t dev;
struct uio *uiop;
struct cred *cr;
{
    return zz9000rw(dev, UIO_WRITE, uiop);
}

int
zz9000mmap(dev, offset, maxprot)
dev_t dev;
off_t offset;
int maxprot;
{
    int unit;
    struct zz9k_board *bp;

    unit = getminor(dev);
    if (!zz9k_probe(unit))
        return -1;
    bp = &zz9k_boards[unit];
    if (offset < 0 || (unsigned long)offset >= bp->board_size)
        return -1;

    return phystopfn((char *)bp->base + offset);
}

int
zz9000ioctl(dev, cmd, arg, mode, cr, rvalp)
dev_t dev;
int cmd;
int arg;
int mode;
struct cred *cr;
int *rvalp;
{
    int unit;
    int error;
    unsigned long pos, limit;
    unsigned short id, bpp;
    unsigned char fill;
    unsigned char *dst;
    struct zz9k_board *bp;
    struct zz9k_info info;
    struct zz9k_fbinfo fbi;
    struct zz9k_mode sm;
    struct zz9k_pan pan;
    struct zz9k_fill zf;
    struct zz9k_fillrect fr;
    struct zz9k_copyrect crct;
    struct zz9k_reg zr;
    struct zz9k_autocon_dump dump;

    unit = getminor(dev);
    if (unit >= ZZ9K_MAXBOARDS)
        return ENXIO;
    bp = &zz9k_boards[unit];

    switch (cmd) {
    case ZZ9KIOC_GETAUTOCON:
        zz9k_fill_autocon(&dump);
        if (copyout((caddr_t)&dump, (caddr_t)arg, sizeof dump))
            return EFAULT;
        return 0;

    case ZZ9KIOC_PROBE:
        if (!zz9k_probe(unit))
            return ENXIO;
        return 0;
    }

    if (!zz9k_probe(unit))
        return ENXIO;

    switch (cmd) {
    case ZZ9KIOC_GETINFO:
        info.base = (unsigned long)bp->base;
        info.board_size = bp->board_size;
        info.fb_offset = ZZ9K_FB_OFFSET;
        info.fb_size = bp->fb_size;
        info.hw_version = bp->hw_version;
        info.fw_version = bp->fw_version;
        info.product = ZZ9K_PRODUCT_Z2;
        info.zorro = 2;
        info.width = bp->width;
        info.height = bp->height;
        info.color_mode = bp->color_mode;
        info.mode = bp->mode;
        if (copyout((caddr_t)&info, (caddr_t)arg, sizeof info))
            return EFAULT;
        break;

    case ZZ9KIOC_GETFBINFO:
        zz9k_fill_fbinfo(bp, &fbi);
        if (copyout((caddr_t)&fbi, (caddr_t)arg, sizeof fbi))
            return EFAULT;
        break;

    case ZZ9KIOC_SETMODE:
        if (copyin((caddr_t)arg, (caddr_t)&sm, sizeof sm))
            return EFAULT;
        id = zz9k_mode_id(sm.width, sm.height);
        bpp = zz9k_bpp(sm.color_mode);
        if (id == 0xffff || bpp == 0)
            return EINVAL;
        if ((unsigned long)sm.width * sm.height * bpp > bp->fb_size)
            return ENOMEM;
        zz9k_set_mode_hw(bp, id, sm.color_mode, sm.scale_mode);
        bp->width = sm.width;
        bp->height = sm.height;
        break;

    case ZZ9KIOC_SETPAN:
        if (copyin((caddr_t)arg, (caddr_t)&pan, sizeof pan))
            return EFAULT;
        if (pan.offset >= bp->fb_size)
            return EINVAL;
        zz9k_write32(bp->base, ZZ9K_REG_PAN_HI, pan.offset);
        break;

    case ZZ9KIOC_SETSWITCH:
        if (arg != ZZ9K_SWITCH_RTG && arg != ZZ9K_SWITCH_CAPTURE)
            return EINVAL;
        zz9k_set_switch_hw(bp, (unsigned short)arg);
        break;

    case ZZ9KIOC_FILL:
        if (copyin((caddr_t)arg, (caddr_t)&zf, sizeof zf))
            return EFAULT;
        if (zf.bytes == 0)
            break;
        if (zf.offset >= bp->fb_size)
            return EINVAL;
        if (zf.bytes > bp->fb_size - zf.offset)
            limit = bp->fb_size;
        else
            limit = zf.offset + zf.bytes;
        fill = (unsigned char)zf.value;
        dst = (unsigned char *)bp->base + ZZ9K_FB_OFFSET + zf.offset;
        for (pos = zf.offset; pos < limit; ++pos)
            *dst++ = fill;
        break;

    case ZZ9KIOC_FILLRECT:
        if (copyin((caddr_t)arg, (caddr_t)&fr, sizeof fr))
            return EFAULT;
        if (fr.width == 0 || fr.height == 0)
            break;
        if (fr.x >= bp->width || fr.y >= bp->height)
            return EINVAL;
        if ((unsigned long)fr.x + fr.width > bp->width)
            return EINVAL;
        if ((unsigned long)fr.y + fr.height > bp->height)
            return EINVAL;
        if (bp->color_mode != ZZ9K_COLOR_16BIT565)
            return EINVAL;
        zz9k_cpu_fillrect(bp, fr.x, fr.y, fr.width, fr.height,
            (unsigned short)fr.color);
        break;

    case ZZ9KIOC_COPYRECT:
        if (copyin((caddr_t)arg, (caddr_t)&crct, sizeof crct))
            return EFAULT;
        if (crct.width == 0 || crct.height == 0)
            break;
        if (crct.src_x >= bp->width || crct.src_y >= bp->height ||
            crct.dst_x >= bp->width || crct.dst_y >= bp->height)
            return EINVAL;
        if ((unsigned long)crct.src_x + crct.width > bp->width)
            return EINVAL;
        if ((unsigned long)crct.src_y + crct.height > bp->height)
            return EINVAL;
        if ((unsigned long)crct.dst_x + crct.width > bp->width)
            return EINVAL;
        if ((unsigned long)crct.dst_y + crct.height > bp->height)
            return EINVAL;
        if (bp->color_mode != ZZ9K_COLOR_16BIT565)
            return EINVAL;
        zz9k_cpu_copyrect(bp, crct.src_x, crct.src_y, crct.dst_x,
            crct.dst_y, crct.width, crct.height);
        break;

    case ZZ9KIOC_GETREG:
        if (copyin((caddr_t)arg, (caddr_t)&zr, sizeof zr))
            return EFAULT;
        if (zr.offset >= ZZ9K_REG_SIZE || (zr.offset & 1))
            return EINVAL;
        zr.value = ZZ9K_REG(bp->base, zr.offset);
        if (copyout((caddr_t)&zr, (caddr_t)arg, sizeof zr))
            return EFAULT;
        break;

    case ZZ9KIOC_SETREG:
        if (copyin((caddr_t)arg, (caddr_t)&zr, sizeof zr))
            return EFAULT;
        if (zr.offset >= ZZ9K_REG_SIZE || (zr.offset & 1))
            return EINVAL;
        ZZ9K_REG(bp->base, zr.offset) = zr.value;
        break;

    case ZZ9KIOC_CONSOLE:
        if (arg == ZZ9K_CONSOLE_ENABLE) {
            error = zz9k_console_enable(bp);
            if (error)
                return error;
            printf("zz9000 console enabled\n");
            break;
        }
        if (arg == ZZ9K_CONSOLE_DISABLE) {
            zz9k_console_disable();
            printf("zz9000 console disabled\n");
            break;
        }
        if (arg == ZZ9K_CONSOLE_REDIRECT) {
            error = zz9k_console_redirect_enable(bp);
            if (error)
                return error;
            printf("zz9000 console redirect enabled\n");
            break;
        }
        if (arg == ZZ9K_CONSOLE_TEST) {
            error = zz9k_console_test(bp);
            if (error)
                return error;
            break;
        }
        return EINVAL;

    default:
        return EINVAL;
    }

    return 0;
}
