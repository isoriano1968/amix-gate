/* soft3dtest.c -- first contact with the Z3660 firmware's soft3D (Wazp3D's
 * rasterizer on the Zynq's ARM) from AMIX.  (2026-10-26)
 *
 * The firmware takes its parameters in a block at board+0x4200000 and an
 * operation in REG_ZZ_SOFT3D_OP (0x270), and reads every pointer it is given
 * at the 060's own addresses -- its MMU maps the whole RTG board there -- so
 * everything (prefs, draw state, vertices, target bitmap) lives in board
 * memory here and is passed by its physical board address.  Structures are
 * big-endian, as the AmigaOS Wazp3D library writes them; the firmware
 * converts.  Needs a kernel whose zz9000 driver maps the whole board
 * (-DZZ9K_MAP_BOARD).
 *
 *   soft3dtest [n]       one Gouraud triangle 320x240, shown at the top
 *                        right of the screen; then n triangles timed
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>

/* the AMIX zz9000 driver's ioctls (zz9000.h) */
#define ZZ9K_IOC            ('Z' << 8)
#define ZZ9KIOC_GETINFO     (ZZ9K_IOC | 1)
#define ZZ9KIOC_GETREG      (ZZ9K_IOC | 6)
#define ZZ9KIOC_SETREG      (ZZ9K_IOC | 7)
#define ZZ9KIOC_GETFBINFO   (ZZ9K_IOC | 11)
struct zz9k_info {
    unsigned long base, board_size, fb_offset, fb_size;
    unsigned short hw_version, fw_version, product, zorro;
    unsigned short width, height, color_mode, mode;
};
struct zz9k_fbinfo {
    unsigned long fb_offset, fb_size;
    unsigned short width, height, color_mode, depth, bytes_per_pixel, stride;
    unsigned long red_mask, green_mask, blue_mask, alpha_mask;
    unsigned short red_shift, green_shift, blue_shift, alpha_shift;
};
struct zz9k_reg { unsigned short offset; unsigned short value; };   /* as the driver has it */

/* Z3660 firmware: register, operations (z3660_regs.h), memory (memorymap.h) */
#define REG_ZZ_SOFT3D_OP    0x270
enum { OP_SETBITMAP, OP_SETCLIPPING, OP_SETDRAWSTATE, OP_DRAWPRIMITIVE, OP_DOUPDATE,
       OP_FLUSH, OP_END, OP_CREATETEXTURE, OP_FREETEXTURE, OP_UPDATETEXTURE, OP_START,
       OP_ALLOCZBUFFER, OP_ALLOCIMAGEBUFFER, OP_CLEARZBUFFER, OP_READZSPAN,
       OP_WRITEZSPAN, OP_DEBUG };
#define FB_PHYS             0x00200000UL    /* device offset 0 = board + this */
#define SOFT3D_DATA         0x04200000UL    /* the parameter block, board offset */

/* Wazp3D (firmware wazp3d/Wazp3D.h, Warp3D.h, cybergraphics.h) */
#define PIXFMT_RGB16        5
#define W3D_ZERO            1
#define W3D_ONE             2
#define W3D_Z_LESS          2
#define W3D_PRIMITIVE_TRIANGLES 0
#define W3D_NOW             255             /* no culling */
#define ZMODE(upd, cmp)     ((upd) * 8 + ((cmp) - 1))
#define BLENDREPLACE        (W3D_ONE * 16 + W3D_ZERO)
#define BUTTON_SIZE         136             /* struct button3D */
#define BTN_RENDERER        9               /* 0 image, 1 soft to bitmap, 2 hard */
#define PREFS_SIZE          7760            /* 57 buttons, window, 2 bytes, MaxPolyHack */

struct soft3d_data {                        /* firmware main.c struct Soft3DData */
    unsigned long offset[4];
    unsigned long format[2];
    unsigned short x[2], y[2];
};
struct state3D {                            /* big-endian; the firmware converts */
    unsigned char Changed, ZMode, BlendMode, TexEnvMode;
    unsigned char PerspMode, CullingMode, FogMode, UseGouraud;
    unsigned char UseTex, UseFog, pad3, pad4;
    unsigned char CurrentRGBA[4], EnvRGBA[4], FogRGBA[4], BackRGBA[4];
    unsigned long PointSize, LineSize, primitive;
    float FogZmin, FogZmax, FogDensity;
    unsigned long gltex, ST, ST2;
};
struct point3D {                            /* 32 bytes: the firmware's stride */
    float x, y, z, u, v, w;
    unsigned char RGBA[4], pad[4];
};

#define W 320
#define H 240
#define WORK_OFF  0x1000000UL                   /* device offset: 16 MB into VRAM */
#define WORK_SIZE 0x40000UL

static int fd;
static volatile struct soft3d_data *data3d;

extern int gettimeofday();

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static void setreg32(unsigned long off, unsigned long v)
{
    struct zz9k_reg r;
    r.offset = off;     r.value = v >> 16;     ioctl(fd, ZZ9KIOC_SETREG, &r);   /* high, latched */
    r.offset = off + 2; r.value = v & 0xffff;  ioctl(fd, ZZ9KIOC_SETREG, &r);   /* low: writes */
}

static unsigned long getreg32(unsigned long off)
{
    struct zz9k_reg r;
    unsigned long v;
    r.offset = off;     ioctl(fd, ZZ9KIOC_GETREG, &r); v = (unsigned long) r.value << 16;
    r.offset = off + 2; ioctl(fd, ZZ9KIOC_GETREG, &r); v |= r.value;
    return v;
}

static unsigned long op(int code)
{
    setreg32(REG_ZZ_SOFT3D_OP, code);
    return getreg32(REG_ZZ_SOFT3D_OP);
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 2000, i, y;
    struct zz9k_info info;
    struct zz9k_fbinfo fbi;
    char *fbmap, *work, *blk;
    unsigned char *prefs;
    struct state3D *st;
    struct point3D *p;
    unsigned short *target, *screen;
    unsigned long board, a_target, a_prefs, a_state, a_points, sc;
    double t0, t;

    fd = open("/dev/zz9000", O_RDWR);
    if (fd < 0) { perror("/dev/zz9000"); return 1; }
    if (ioctl(fd, ZZ9KIOC_GETINFO, &info) < 0 || ioctl(fd, ZZ9KIOC_GETFBINFO, &fbi) < 0) {
        perror("GETINFO"); return 1;
    }
    board = info.base;
    printf("board 0x%lx, fw 0x%x, screen %ux%u stride %u\n", board, info.fw_version,
           fbi.width, fbi.height, fbi.stride);

    /* device offset 0 is board+FB_PHYS: the screen, then free VRAM; our
       working area 16 MB in, the parameter block at device 0x4000000 */
    /* three small mappings (the default ulimit -v is 16 MB): the visible
       screen, a 256 KB work area 16 MB into VRAM, the parameter block */
    fbmap = (char *) mmap(0, (long) fbi.stride * fbi.height, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fbmap == (char *) -1) { perror("mmap screen"); return 1; }
    work = (char *) mmap(0, WORK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t) WORK_OFF);
    if (work == (char *) -1) { perror("mmap work area"); return 1; }
    blk = (char *) mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                        (off_t)(SOFT3D_DATA - FB_PHYS));
    if (blk == (char *) -1) { perror("mmap soft3D block (needs -DZZ9K_MAP_BOARD)"); return 1; }
    data3d = (volatile struct soft3d_data *) blk;

    screen = (unsigned short *) fbmap;
    target = (unsigned short *) work;
    prefs  = (unsigned char *) (work + 0x30000);
    st     = (struct state3D *) (work + 0x33000);
    p      = (struct point3D *) (work + 0x34000);
#define AMIGA(ptr) (board + FB_PHYS + WORK_OFF + (unsigned long)((char *)(ptr) - work))
    a_target = AMIGA(target); a_prefs = AMIGA(prefs); a_state = AMIGA(st); a_points = AMIGA(p);

    memset(prefs, 0, PREFS_SIZE);
    prefs[BTN_RENDERER * BUTTON_SIZE + 1] = 1;          /* Renderer.ON: soft to bitmap */
    memset(target, 0, W * H * 2);

    data3d->offset[0] = a_prefs;
    sc = op(OP_START);
    printf("Start -> SC 0x%08lx\n", sc);
    if (!sc) { printf("soft3D did not start\n"); return 1; }

    data3d->offset[0] = sc; data3d->x[0] = W; data3d->y[0] = H;
    printf("AllocZbuffer -> 0x%08lx\n", op(OP_ALLOCZBUFFER));

    data3d->offset[0] = sc; data3d->offset[1] = a_prefs; data3d->offset[2] = a_target;
    data3d->format[0] = PIXFMT_RGB16;
    data3d->x[0] = 0; data3d->y[0] = 0; data3d->x[1] = W; data3d->y[1] = H;
    op(OP_SETBITMAP);

    data3d->offset[0] = sc; data3d->x[0] = 0; data3d->x[1] = W - 1; data3d->y[0] = 0; data3d->y[1] = H - 1;
    op(OP_SETCLIPPING);

    memset(st, 0, sizeof *st);
    st->Changed = 1;
    st->ZMode = ZMODE(1, W3D_Z_LESS);
    st->BlendMode = BLENDREPLACE;
    st->UseGouraud = 1;
    st->CullingMode = W3D_NOW;
    st->CurrentRGBA[0] = st->CurrentRGBA[1] = st->CurrentRGBA[2] = st->CurrentRGBA[3] = 255;
    st->PointSize = st->LineSize = 1;
    st->primitive = W3D_PRIMITIVE_TRIANGLES;
    data3d->offset[0] = sc; data3d->offset[1] = a_state;
    op(OP_SETDRAWSTATE);

    {   float one = 1.0f;
        data3d->offset[0] = sc; memcpy((void *) &data3d->format[0], &one, 4);
        op(OP_CLEARZBUFFER); }

    memset(p, 0, 3 * sizeof *p);
    p[0].x = 160; p[0].y = 20;  p[0].z = 0.5f; p[0].w = 1; p[0].RGBA[0] = 255; p[0].RGBA[3] = 255;
    p[1].x = 300; p[1].y = 220; p[1].z = 0.5f; p[1].w = 1; p[1].RGBA[1] = 255; p[1].RGBA[3] = 255;
    p[2].x = 20;  p[2].y = 220; p[2].z = 0.5f; p[2].w = 1; p[2].RGBA[2] = 255; p[2].RGBA[3] = 255;
    data3d->offset[0] = sc; data3d->offset[1] = a_points;
    data3d->format[0] = 3; data3d->format[1] = W3D_PRIMITIVE_TRIANGLES;
    op(OP_DRAWPRIMITIVE);
    data3d->offset[0] = sc;
    op(OP_FLUSH);
    data3d->offset[0] = sc;
    printf("DoUpdate -> %lu\n", op(OP_DOUPDATE));

    {   int lit = 0;
        for (i = 0; i < W * H; i++)
            if (target[i])
                lit++;
        printf("pixels drawn: %d of %d (a full triangle is about %d)\n", lit, W * H, 280 * 200 / 2);
        printf("centre pixel 0x%04x, near red corner 0x%04x\n", target[130 * W + 160], target[30 * W + 160]); }

    /* show it at the top right of the screen */
    for (y = 0; y < H; y++)
        memcpy(screen + (40 + y) * (fbi.stride / 2) + fbi.width - W - 40, target + y * W, W * 2);

    /* speed: n triangles, one DrawPrimitive each, then one with all */
    t0 = now();
    for (i = 0; i < n; i++) {
        data3d->offset[0] = sc; data3d->offset[1] = a_points;
        data3d->format[0] = 3; data3d->format[1] = W3D_PRIMITIVE_TRIANGLES;
        op(OP_DRAWPRIMITIVE);
    }
    data3d->offset[0] = sc; op(OP_DOUPDATE);
    t = now() - t0;
    printf("%d triangles (one call each, ~28000 pixels each): %.2f s = %.0f tri/s, %.1f Mpixel/s\n",
           n, t, n / t, n * 28000.0 / t / 1e6);

    data3d->offset[0] = sc;
    op(OP_END);
    return 0;
}
