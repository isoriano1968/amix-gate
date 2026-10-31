/*
 * s3d.c -- libs3d: the Z3660's soft3D for any rasterizer front end on AMIX
 * (2026-10-26).  The protocol and the traps are those of TinyGL's
 * zsoft3d.c (tinygl-amix), where they were found; in short:
 *
 *  - The firmware (shanshe/Z3660 wazp3d/soft3d56.c) takes its parameters in
 *    a block at board+0x4200000 and the operation in REG_ZZ_SOFT3D_OP, every
 *    pointer a 68k board address, structures big-endian.  Needs a kernel
 *    whose zz9000 driver maps the whole board and has ZZ9KIOC_SOFT3D and
 *    ZZ9KIOC_BLITX (-DZZ9K_MAP_BOARD).
 *  - Its heap is 16 MB and SOFT3D_End frees neither the Z buffer nor the
 *    textures; a full heap makes ALLOCZBUFFER return NULL and the next draw
 *    takes the ARM, and the 68k with it, down.  So everything is given back
 *    at exit and on SIGINT/TERM/HUP, and an owner record in board memory
 *    lets the next process free what a killed one left (shared with
 *    TinyGL's zsoft3d.c: same place, same layout).
 *  - The Z buffer must be as wide as the bitmap's rows (CLEARZBUFFER clears
 *    bitmap-large x high entries).
 *  - The ARM trusts what it gets: every triangle is checked (finite, on the
 *    bitmap, an area) before it goes.
 *  - Blending: Wazp3D reads the destination only when its fragment buffer
 *    is flushed, so overlapping blended triangles in one buffer do not
 *    layer; overlaps are detected and flushed in between.
 *  - Colours go as A B G R (the firmware byte-reverses the long).
 *
 * Board memory, device offsets (device 0 = board + 0x200000):
 *   rows 0..1079       the screen (Xzz9000)
 *   rows 1100..        the back buffer, at the screen's stride
 *   0x0C00000-0x23FFFFF  textures (24 MB, mapped a texture at a time)
 *   0x2400000          prefs, state, vertex batch, owner record
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <utmp.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <X11/Xlibint.h>
#include "s3d.h"

extern int munmap();      /* not in AMIX's sys/mman.h */

/* the AMIX zz9000 driver (zz9000.h) */
#define ZZ9K_IOC            ('Z' << 8)
#define ZZ9KIOC_GETINFO     (ZZ9K_IOC | 1)
#define ZZ9KIOC_GETFBINFO   (ZZ9K_IOC | 11)
#define ZZ9KIOC_SOFT3D      (ZZ9K_IOC | 17)
#define ZZ9KIOC_BLITX       (ZZ9K_IOC | 18)
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
struct zz9k_soft3d { unsigned long op, result; };
struct zz9k_blitx {
    unsigned short op, sx, sy, dx, dy, width, height, pad;
    unsigned long color;
};

/* the firmware */
enum { OP_SETBITMAP, OP_SETCLIPPING, OP_SETDRAWSTATE, OP_DRAWPRIMITIVE, OP_DOUPDATE,
       OP_FLUSH, OP_END, OP_CREATETEXTURE, OP_FREETEXTURE, OP_UPDATETEXTURE, OP_START,
       OP_ALLOCZBUFFER, OP_ALLOCIMAGEBUFFER, OP_CLEARZBUFFER };
#define FB_PHYS             0x00200000UL
#define SOFT3D_DATA         0x04200000UL
#define PIXFMT_RGB16        5
#define W3D_R8G8B8          4
#define W3D_R8G8B8A8        11
#define W3D_PRIMITIVE_TRIANGLES 0
#define PERSP_CORRECT       1
#define W3D_NOW             255
#define BUTTON_SIZE         136
#define BTN_RENDERER        9
#define PREFS_SIZE          7760
#define MAXPOINTS           2997

struct soft3d_data {
    unsigned long offset[4];
    unsigned long format[2];
    unsigned short x[2], y[2];
};
struct state3D {
    unsigned char Changed, ZMode, BlendMode, TexEnvMode;
    unsigned char PerspMode, CullingMode, FogMode, UseGouraud;
    unsigned char UseTex, UseFog, pad3, pad4;
    unsigned char CurrentRGBA[4], EnvRGBA[4], FogRGBA[4], BackRGBA[4];
    unsigned long PointSize, LineSize, primitive;
    float FogZmin, FogZmax, FogDensity;
    unsigned long gltex, ST, ST2;
};
struct point3D {
    float x, y, z, u, v, w;
    unsigned char RGBA[4], pad[4];
};

#define BACK_ROW            1100
#define DATA_OFF            0x2400000UL
#define DATA_SIZE           0x40000UL
#define TEX_OFF             0x0C00000UL
#define TEX_SIZE            0x1800000UL
#define TEX_PAGE            4096UL

/* the owner record (zsoft3d.c has the same) */
#define OWNER_OFF           0x38000
#define OWNER_MAGIC         0x53334f57UL        /* "S3OW" */
#define MAXTEX              4096
struct owner {
    unsigned long magic;
    long boot, pid;
    unsigned long sc, ntex;
    unsigned long tex[MAXTEX];
};

struct s3d_tex {
    unsigned long st;           /* the firmware's texture */
    unsigned long off, bytes;   /* its texels: device offset, size */
    int w, h, bpp;
    int slot;                   /* in the owner record */
};

static int fd = -1, active, failed;
static const char *who = "libs3d";
static int verbose = 1;                /* s3d_verbose */
static volatile struct soft3d_data *blk;
static char *data;
static struct state3D *state;
static volatile struct owner *own;
static struct point3D batch[MAXPOINTS + 3] __attribute__((aligned(16)));
static struct point3D *bbatch;
static unsigned long a_prefs, a_state, a_batch, board, sc;
static int nbatch, width, height, screen_w, screen_h, stride_px, fb_rows;
static unsigned short *back;            /* the back buffer, mapped */
static unsigned long back_len;
static int xmajor;
static struct s3d_state cur;
static int cur_valid;
static struct s3d_state want;           /* set, not yet sent: see s3d_set_state */
static int want_valid;
static unsigned long st_tris, st_blended, st_sat, st_flushes;   /* S3D_STATS */
static unsigned long st_states, st_draws, st_finishes, st_texup;

#define AMIGA(ptr) (board + FB_PHYS + DATA_OFF + (unsigned long)((char *)(ptr) - data))

#ifdef S3D_FAKE
/* PC profiling (-DS3D_FAKE): no board -- every operation succeeds and does
   nothing, so only the 68k side of the work is left to measure */
static int fake;
#endif

/* experiments: S3D_NODRAW=1 (the ARM draws nothing), S3D_NOORDER=1 (no
   overlap flushes for blended triangles); S3D_ORDER=1 orders every blended
   triangle, also under a depth test (see blend_order) */
static int nodraw = -1, noorder, order_all, nostate;

static unsigned long op(int code)
{
    struct zz9k_soft3d s;

    if (nodraw < 0) {
        nodraw = getenv("S3D_NODRAW") != NULL;
        noorder = getenv("S3D_NOORDER") != NULL;
        order_all = getenv("S3D_ORDER") != NULL;
        nostate = getenv("S3D_NOSTATE") != NULL;
    }
    if (nostate && code == OP_SETDRAWSTATE)
        return 0;
    if (nodraw && (code == OP_DRAWPRIMITIVE || code == OP_FLUSH || code == OP_CLEARZBUFFER))
        return 0;

#ifdef S3D_FAKE
    if (fake)
        return code == OP_START || code == OP_CREATETEXTURE || code == OP_ALLOCZBUFFER;
#endif
    s.op = code;
    s.result = 0;
    if (ioctl(fd, ZZ9KIOC_SOFT3D, &s) < 0)
        return 0;
    return s.result;
}

static int blitx(int o, int sx, int sy, int dx, int dy, int w, int h, unsigned long color)
{
    struct zz9k_blitx b;

    b.op = o; b.sx = sx; b.sy = sy; b.dx = dx; b.dy = dy;
    b.width = w; b.height = h; b.pad = 0; b.color = color;
#ifdef S3D_FAKE
    if (fake)
        return 0;
#endif
    return ioctl(fd, ZZ9KIOC_BLITX, &b);
}

/* --- GL enums ---------------------------------------------------------- */

int s3d_gl_factor(int f)
{
    switch (f) {
    case 0:      return 1;              /* GL_ZERO */
    case 1:      return 2;              /* GL_ONE */
    case 0x0300: return 3;              /* GL_SRC_COLOR */
    case 0x0301: return 5;              /* GL_ONE_MINUS_SRC_COLOR */
    case 0x0302: return 7;              /* GL_SRC_ALPHA */
    case 0x0303: return 8;              /* GL_ONE_MINUS_SRC_ALPHA */
    case 0x0304: return 9;              /* GL_DST_ALPHA */
    case 0x0305: return 10;             /* GL_ONE_MINUS_DST_ALPHA */
    case 0x0306: return 4;              /* GL_DST_COLOR */
    case 0x0307: return 6;              /* GL_ONE_MINUS_DST_COLOR */
    case 0x0308: return 11;             /* GL_SRC_ALPHA_SATURATE */
    }
    return 0;
}

int s3d_gl_depthfunc(int f)
{
    switch (f) {
    case 0x0200: return S3D_Z_NEVER;
    case 0x0201: return S3D_Z_LESS;
    case 0x0202: return S3D_Z_EQUAL;
    case 0x0203: return S3D_Z_LEQUAL;
    case 0x0204: return S3D_Z_GREATER;
    case 0x0205: return S3D_Z_NOTEQUAL;
    case 0x0206: return S3D_Z_GEQUAL;
    case 0x0207: return S3D_Z_ALWAYS;
    }
    return S3D_Z_LESS;
}

int s3d_gl_texenv(int e)
{
    switch (e) {
    case 0x1E01: return S3D_ENV_REPLACE;
    case 0x2101: return S3D_ENV_DECAL;
    case 0x0BE2: return S3D_ENV_BLEND;
    }
    return S3D_ENV_MODULATE;            /* GL_MODULATE, GL's default */
}

/* --- owner record, cleanup ---------------------------------------------- */

static void release(unsigned long ctx, volatile struct owner *o)
{
    unsigned long i;

    for (i = 0; i < o->ntex && i < MAXTEX; i++)
        if (o->tex[i]) {
            blk->offset[0] = ctx; blk->offset[1] = o->tex[i];
            op(OP_FREETEXTURE);
            o->tex[i] = 0;
        }
    blk->offset[0] = ctx; blk->x[0] = 1; blk->y[0] = 1;
    op(OP_ALLOCZBUFFER);                        /* frees the big one */
    blk->offset[0] = ctx;
    op(OP_END);
    o->ntex = 0;
    o->magic = 0;
}

static long boot_time(void)
{
    struct utmp u, *b;
    long t = 0;

    setutent();
    u.ut_type = BOOT_TIME;
    if ((b = getutid(&u)) != NULL)
        t = (long) b->ut_time;
    endutent();
    return t;
}

static void s3d_end(void)
{
    if (fd < 0 || !sc)
        return;
    if (own->magic == OWNER_MAGIC && own->pid == (long) getpid())
        release(sc, own);
    sc = 0;
    active = 0;
}

static void on_signal(int sig)
{
    s3d_end();
    signal(sig, SIG_DFL);
    kill(getpid(), sig);
}

/* --- the X server's ZZ9K-SOFT3D --------------------------------------- */

#define X_ZZ9KSwap 0
typedef struct {
    CARD8  reqType;
    CARD8  s3dReqType;
    CARD16 length B16;
    CARD32 window B32;
    INT16  srcX B16, srcY B16;
    INT16  x B16, y B16;
    CARD16 width B16, height B16;
} xZZ9KSwapReq;
#define sz_xZZ9KSwapReq 20

static int xquery(Display *dpy)
{
    int major, ev, err;

    return XQueryExtension(dpy, "ZZ9K-SOFT3D", &major, &ev, &err) ? major : 0;
}

static void xswap(Display *dpy, Window win, int sx, int sy, int w, int h)
{
    xZZ9KSwapReq *req;

    LockDisplay(dpy);
    GetReq(ZZ9KSwap, req);
    req->reqType = xmajor;
    req->s3dReqType = X_ZZ9KSwap;
    req->window = win;
    req->srcX = sx;
    req->srcY = sy;
    req->x = 0;
    req->y = 0;
    req->width = w;
    req->height = h;
    UnlockDisplay(dpy);
    SyncHandle();
    XSync(dpy, False);          /* copied before the next frame overwrites it */
}

/* --- start ---------------------------------------------------------------- */

/* 0: say nothing when soft3D is not there (the caller falls back quietly) */
void s3d_verbose(int on)
{
    verbose = on;
}

int s3d_init(Display *dpy, const char *name)
{
    struct zz9k_info info;
    struct zz9k_fbinfo fbi;
    char *p;

    if (active)
        return 0;
    if (failed || fd >= 0)
        return -1;
    if (name)
        who = name;
#ifdef S3D_FAKE
    if (getenv("S3D_FAKE")) {
        fake = 1;
        fd = 0;
        board = 0x50000000;
        screen_w = 1920; screen_h = 1080; stride_px = 1920; fb_rows = 12561;
        data = (char *) calloc(1, DATA_SIZE);
        blk = (volatile struct soft3d_data *) calloc(1, 0x1000);
        own = (volatile struct owner *) (data + OWNER_OFF);
        state = (struct state3D *) (data + 0x2000);
        bbatch = (struct point3D *) (data + 0x4000);
        sc = 1;
        active = 1;
        return 0;
    }
#endif
    if (getenv("S3D_DIRECT") && atoi(getenv("S3D_DIRECT")) > 0)
        xmajor = -1;
    else if ((xmajor = xquery(dpy)) == 0) {
        if (verbose)
            fprintf(stderr, "%s: the X server has no ZZ9K-SOFT3D (an older Xzz9000); no soft3D\n", who);
        failed = 1;
        return -1;
    }
    fd = open("/dev/zz9000", O_RDWR);
    if (fd < 0)
        goto fail;
    if (ioctl(fd, ZZ9KIOC_GETINFO, &info) < 0 || ioctl(fd, ZZ9KIOC_GETFBINFO, &fbi) < 0)
        goto fail;
    board = info.base;
    screen_w = fbi.width;
    screen_h = fbi.height;
    stride_px = fbi.stride / 2;
    fb_rows = fbi.fb_size / fbi.stride;
    data = (char *) mmap(0, DATA_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t) DATA_OFF);
    if (data == (char *) -1)
        goto fail;
    p = (char *) mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                      (off_t) (SOFT3D_DATA - FB_PHYS));
    if (p == (char *) -1)
        goto fail;
    blk = (volatile struct soft3d_data *) p;
    own = (volatile struct owner *) (data + OWNER_OFF);
    if (own->magic == OWNER_MAGIC && own->boot == boot_time() && own->pid != (long) getpid()) {
        if (kill((pid_t) own->pid, 0) == 0 || errno == EPERM) {
            fprintf(stderr, "%s: soft3D is in use by process %ld\n", who, own->pid);
            close(fd);
            fd = -1;
            failed = 1;
            return -1;
        }
        release(own->sc, own);                  /* a dead owner's leftovers */
    }
    state = (struct state3D *) (data + 0x2000);
    bbatch = (struct point3D *) (data + 0x4000);
    a_prefs = AMIGA(data); a_state = AMIGA(state); a_batch = AMIGA(bbatch);
    memset(data, 0, PREFS_SIZE);
    data[BTN_RENDERER * BUTTON_SIZE + 1] = 1;   /* soft to bitmap */
    blk->offset[0] = a_prefs;
    sc = op(OP_START);
    if (!sc)
        goto fail;
    own->sc = sc;
    own->ntex = 0;
    own->pid = (long) getpid();
    own->boot = boot_time();
    own->magic = OWNER_MAGIC;
    atexit(s3d_end);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    active = 1;
    return 0;
fail:
    if (verbose)
        fprintf(stderr, "%s: soft3D not available (needs the Z3660 and a ZZ9K_MAP_BOARD kernel)\n", who);
    if (fd >= 0)
        close(fd);
    fd = -1;
    failed = 1;
    return -1;
}

int s3d_active(void)
{
    return active;
}

int s3d_resize(int w, int h)
{
    if (!active)
        return -1;
    s3d_finish();
    if (w > screen_w)
        w = screen_w;
    if (h > fb_rows - BACK_ROW)
        h = fb_rows - BACK_ROW;
    if (w < 1 || h < 1)
        return -1;
    width = w;
    height = h;
    blk->offset[0] = sc; blk->offset[1] = a_prefs;
    blk->offset[2] = board + FB_PHYS + (unsigned long) BACK_ROW * stride_px * 2;
    blk->format[0] = PIXFMT_RGB16;
    blk->x[0] = 0; blk->y[0] = 0; blk->x[1] = stride_px; blk->y[1] = h;
    op(OP_SETBITMAP);
    blk->offset[0] = sc; blk->x[0] = stride_px; blk->y[0] = h;
    if (!op(OP_ALLOCZBUFFER)) {
        fprintf(stderr, "%s: soft3D has no memory for a %dx%d Z buffer\n", who, w, h);
        s3d_end();
        failed = 1;
        return -1;
    }
    blk->offset[0] = sc; blk->x[0] = 0; blk->x[1] = w - 1; blk->y[0] = 0; blk->y[1] = h - 1;
    op(OP_SETCLIPPING);
    if (back) {
#ifdef S3D_FAKE
        if (fake)
            free(back);
        else
#endif
        munmap((caddr_t) back, back_len);
        back = 0;
    }
    cur_valid = want_valid = 0;
    nbatch = 0;
    return 0;
}

/* --- state ---------------------------------------------------------------- */

static int nov;                         /* blended triangles in the fragment buffer */

/* A state change costs the firmware about 1.2 ms (2026-10-27, the A3000),
   so s3d_set_state only records it; it goes to the board with the first
   triangle drawn in it.  States nothing is drawn in cost nothing. */
void s3d_set_state(const struct s3d_state *d)
{
    want = *d;
    want_valid = 1;
}

/* the state the next triangle is drawn in; 0 if there is none */
int s3d_get_state(struct s3d_state *d)
{
    if (want_valid)
        *d = want;
    else if (cur_valid)
        *d = cur;
    else
        return 0;
    return 1;
}

static void apply_state(const struct s3d_state *d)
{
    struct state3D st;
    unsigned long *to, *from;
    unsigned i;

    if (!active)
        return;
    if (cur_valid && d->tex == cur.tex && d->blend == cur.blend &&
        d->zmode == cur.zmode && d->texenv == cur.texenv)
        return;
    s3d_flush();
    st_states++;
    /* built here, then written to the board a long at a time (libc's
       memset and byte stores are slow over Zorro III) */
    memset(&st, 0, sizeof st);
    st.Changed = 1;
    st.ZMode = d->zmode;
    st.BlendMode = d->blend;
    st.UseGouraud = 1;
    st.CullingMode = W3D_NOW;                   /* the caller culls */
    st.UseTex = d->tex != 0;
    st.TexEnvMode = d->texenv;
    st.PerspMode = PERSP_CORRECT;
    st.CurrentRGBA[0] = st.CurrentRGBA[1] = st.CurrentRGBA[2] = st.CurrentRGBA[3] = 255;
    st.PointSize = st.LineSize = 1;
    st.primitive = W3D_PRIMITIVE_TRIANGLES;
    st.ST = d->tex ? d->tex->st : 0;
    to = (unsigned long *) state;
    from = (unsigned long *) &st;
    for (i = 0; i < sizeof st / 4; i++)
        to[i] = from[i];
    blk->offset[0] = sc; blk->offset[1] = a_state;
    op(OP_SETDRAWSTATE);                        /* flushes the fragments */
    nov = 0;
    cur = *d;
    cur_valid = 1;
}

/* --- triangles ------------------------------------------------------------ */

static void to_board(struct point3D *dst, const struct point3D *src, int n)
{
#ifdef __m68k__
    register const struct point3D *s __asm__("a0") = src;
    register struct point3D *d __asm__("a1") = dst;

    if (((unsigned long) src | (unsigned long) dst) & 15) {
        memcpy(dst, src, n * sizeof *src);
        return;
    }
    while (n-- > 0)
        __asm__ volatile (".word 0xf620,0x9000\n\t"     /* move16 (a0)+,(a1)+ */
                          ".word 0xf620,0x9000"
                          : "+a" (s), "+a" (d) : : "memory");
#else
    memcpy(dst, src, n * sizeof *src);          /* S3D_FAKE on the PC */
#endif
}

void s3d_flush(void)
{
    if (!active || nbatch == 0)
        return;
    to_board(bbatch, batch, nbatch);
    st_draws++;
    blk->offset[0] = sc; blk->offset[1] = a_batch;
    blk->format[0] = nbatch; blk->format[1] = W3D_PRIMITIVE_TRIANGLES;
    op(OP_DRAWPRIMITIVE);
    nbatch = 0;
}

void s3d_finish(void)
{
    if (!active)
        return;
    s3d_flush();
    st_finishes++;
    blk->offset[0] = sc;
    op(OP_FLUSH);
    nov = 0;
}

/* every test is written so that it holds for good values: a NaN fails
   any comparison, so it is rejected without a test of its own, and an
   infinity is out of range (8,500 triangles a Quake III frame: it counts).
   Callers build with -ffast-math, which would let gcc assume no NaNs and
   turn !(a >= b) into a < b: not here. */
__attribute__((optimize("no-finite-math-only")))
static int sane(struct point3D *p)
{
    float a, xmax = width + 1.0f, ymax = height + 1.0f;
    int i;

    for (i = 0; i < 3; i++) {
        if (!(p[i].x >= -1.0f && p[i].x <= xmax && p[i].y >= -1.0f && p[i].y <= ymax &&
              p[i].z >= -0.01f && p[i].z <= 1.01f && p[i].w > 0.0f && p[i].w < 1e30f &&
              p[i].u > -1e6f && p[i].u < 1e6f && p[i].v > -1e6f && p[i].v < 1e6f))
            return 0;
        if (p[i].x < 0.0f) p[i].x = 0.0f;
        if (p[i].x > width - 1) p[i].x = width - 1;
        if (p[i].y < 0.0f) p[i].y = 0.0f;
        if (p[i].y > height - 1) p[i].y = height - 1;
        if (p[i].z < 0.0f) p[i].z = 0.0f;
        if (p[i].z > 1.0f) p[i].z = 1.0f;
    }
    a = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y);
    return a >= 0.01f || a <= -0.01f;
}

#define MAXOV 48
static float ov[MAXOV][6];
static float ovbox[MAXOV][4];           /* their bounding boxes: x0 y0 x1 y1 */

static int separated(const float *a, const float *b)
{
    int i, k, j;
    const float *t;

    for (k = 0; k < 2; k++) {
        t = k ? b : a;
        for (i = 0; i < 3; i++) {
            float nx = t[((i + 1) % 3) * 2 + 1] - t[i * 2 + 1];
            float ny = t[i * 2] - t[((i + 1) % 3) * 2];
            float amin = 1e30f, amax = -1e30f, bmin = 1e30f, bmax = -1e30f, d;
            for (j = 0; j < 3; j++) {
                d = a[j * 2] * nx + a[j * 2 + 1] * ny;
                if (d < amin) amin = d;
                if (d > amax) amax = d;
                d = b[j * 2] * nx + b[j * 2 + 1] * ny;
                if (d < bmin) bmin = d;
                if (d > bmax) bmax = d;
            }
            d = 0.01f * ((nx < 0 ? -nx : nx) + (ny < 0 ? -ny : ny));
            if (amax <= bmin + d || bmax <= amin + d)
                return 1;
        }
    }
    return 0;
}

static void blend_order(void)
{
    struct point3D *p = &batch[nbatch - 3], keep[3];
    float t[6];
    int i;

    float bx0, by0, bx1, by1;

    st_blended++;
    for (i = 0; i < 3; i++) {
        t[i * 2] = p[i].x;
        t[i * 2 + 1] = p[i].y;
    }
    bx0 = bx1 = t[0];
    by0 = by1 = t[1];
    for (i = 1; i < 3; i++) {
        if (t[i * 2] < bx0) bx0 = t[i * 2];
        if (t[i * 2] > bx1) bx1 = t[i * 2];
        if (t[i * 2 + 1] < by0) by0 = t[i * 2 + 1];
        if (t[i * 2 + 1] > by1) by1 = t[i * 2 + 1];
    }
    /* boxes first: only triangles whose boxes meet need the exact test */
    for (i = 0; i < nov; i++)
        if (bx0 < ovbox[i][2] && ovbox[i][0] < bx1 && by0 < ovbox[i][3] && ovbox[i][1] < by1) {
            st_sat++;
            if (!separated(t, ov[i]))
                break;
        }
    if (i < nov || nov == MAXOV) {
        memcpy(keep, p, sizeof keep);
        nbatch -= 3;
        s3d_finish();
        st_flushes++;
        memcpy(&batch[0], keep, sizeof keep);
        nbatch = 3;
    }
    ovbox[nov][0] = bx0; ovbox[nov][1] = by0; ovbox[nov][2] = bx1; ovbox[nov][3] = by1;
    memcpy(ov[nov++], t, sizeof t);
}

static void put(struct point3D *p, const struct s3d_vertex *v)
{
    p->x = v->x; p->y = v->y; p->z = v->z; p->w = v->w;
    p->u = v->u; p->v = v->v;
    /* A B G R in memory (see the top): one long on the big-endian 68k */
    *(unsigned long *) p->RGBA = ((unsigned long) v->a << 24) | ((unsigned long) v->b << 16) |
                                 ((unsigned long) v->g << 8) | v->r;
}

void s3d_tri(const struct s3d_vertex *a, const struct s3d_vertex *b, const struct s3d_vertex *c)
{
    if (!active)
        return;
    st_tris++;
    if (want_valid) {
        want_valid = 0;
        apply_state(&want);
    }
    if (nbatch + 3 > MAXPOINTS)
        s3d_flush();
    put(&batch[nbatch], a);
    put(&batch[nbatch + 1], b);
    put(&batch[nbatch + 2], c);
    nbatch += 3;
    /* Blended triangles are ordered (see blend_order) when there is no
       depth test: 2D, HUD and console.  Under a depth test the usual
       multi-pass drawing (Quake III, GLQuake: the passes after the first
       blend over surfaces the first pass put into the Z buffer, with depth
       writes off) lets at most one fragment a pixel through anyway, and
       ordering there cost Quake III 450 ms a frame; overlapping transparent
       effects may lose some of their overlap.  S3D_ORDER=1: order all. */
    if (!sane(&batch[nbatch - 3]))
        nbatch -= 3;
    else if (cur.blend != S3D_REPLACE && !noorder &&
             (order_all || (cur.zmode & 7) == S3D_Z_ALWAYS - 1))
        blend_order();
}

/* a triangle the caller has clipped to the drawing area (x, y in range up to
   rounding, w > 0): only what can still go wrong is checked -- NaNs, x and y
   a hair outside, z rounding past 0..1, zero area */
__attribute__((optimize("no-finite-math-only")))
void s3d_tri_clipped(const struct s3d_vertex *a, const struct s3d_vertex *b, const struct s3d_vertex *c)
{
    struct point3D *p;
    float xm = width - 1, ym = height - 1, ar;
    int i;

    if (!active)
        return;
    st_tris++;
    if (want_valid) {
        want_valid = 0;
        apply_state(&want);
    }
    if (nbatch + 3 > MAXPOINTS)
        s3d_flush();
    p = &batch[nbatch];
    put(&p[0], a);
    put(&p[1], b);
    put(&p[2], c);
    for (i = 0; i < 3; i++) {
        if (!(p[i].x >= -1.0f && p[i].y >= -1.0f && p[i].x <= xm + 2.0f && p[i].y <= ym + 2.0f &&
              p[i].z > -1.0f && p[i].z < 2.0f &&
              p[i].u > -1e6f && p[i].u < 1e6f && p[i].v > -1e6f && p[i].v < 1e6f))
            return;                             /* NaN or worse */
        if (p[i].x < 0.0f) p[i].x = 0.0f; else if (p[i].x > xm) p[i].x = xm;
        if (p[i].y < 0.0f) p[i].y = 0.0f; else if (p[i].y > ym) p[i].y = ym;
        if (p[i].z < 0.0f) p[i].z = 0.0f; else if (p[i].z > 1.0f) p[i].z = 1.0f;
    }
    ar = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y);
    if (!(ar >= 0.01f || ar <= -0.01f))
        return;
    nbatch += 3;
    if (cur.blend != S3D_REPLACE && !noorder && (order_all || (cur.zmode & 7) == S3D_Z_ALWAYS - 1))
        blend_order();
}

/* --- clear, swap, back buffer --------------------------------------------- */

void s3d_clear_color(int x, int y, int w, int h, unsigned short rgb)
{
    if (!active)
        return;
    s3d_finish();
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > width) w = width - x;
    if (y + h > height) h = height - y;
    if (w > 0 && h > 0)
        blitx(0, 0, 0, x, BACK_ROW + y, w, h, rgb);
}

void s3d_clear_depth(float z)
{
    if (!active)
        return;
    s3d_flush();
    blk->offset[0] = sc;
    memcpy((void *) &blk->format[0], &z, 4);
    op(OP_CLEARZBUFFER);
}

void s3d_swap(Display *dpy, Window win)
{
    int x, y, sx = 0, sy = BACK_ROW, w = width, h = height;
    Window child;
    static int stats = -1;
    static unsigned long frames;

    if (!active)
        return;
    if (stats < 0)
        stats = getenv("S3D_STATS") != NULL;
    if (stats && ++frames % 20 == 0) {
        fprintf(stderr, "s3d stats per frame: %lu triangles, %lu blended, %lu exact tests, %lu overlap flushes, "
                "%lu state changes, %lu draws, %lu finishes, %lu texture updates\n",
                st_tris / 20, st_blended / 20, st_sat / 20, st_flushes / 20,
                st_states / 20, st_draws / 20, st_finishes / 20, st_texup / 20);
        st_tris = st_blended = st_sat = st_flushes = st_states = st_draws = st_finishes = st_texup = 0;
    }
    s3d_finish();
    blk->offset[0] = sc;
    op(OP_DOUPDATE);
#ifdef S3D_FAKE
    if (fake)
        return;
#endif
    if (xmajor > 0) {
        xswap(dpy, win, 0, BACK_ROW, w, h);
        return;
    }
    /* S3D_DIRECT: straight onto the screen, over whatever is there */
    XTranslateCoordinates(dpy, win, DefaultRootWindow(dpy), 0, 0, &x, &y, &child);
    if (x < 0) { sx -= x; w += x; x = 0; }
    if (y < 0) { sy -= y; h += y; y = 0; }
    if (x + w > screen_w) w = screen_w - x;
    if (y + h > screen_h) h = screen_h - y;
    if (w > 0 && h > 0)
        blitx(1, sx, sy, x, y, w, h, 0);
}

unsigned short *s3d_backbuffer(int *stride)
{
    char *p;

    if (!active)
        return 0;
    if (!back) {
        back_len = (unsigned long) height * stride_px * 2;
#ifdef S3D_FAKE
        if (fake) {
            back = (unsigned short *) calloc(1, back_len);
            *stride = stride_px;
            return back;
        }
#endif
        p = (char *) mmap(0, back_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                          (off_t) ((unsigned long) BACK_ROW * stride_px * 2));
        if (p == (char *) -1)
            return 0;
        back = (unsigned short *) p;
    }
    *stride = stride_px;
    return back;
}

/* --- textures --------------------------------------------------------------- */

/* the texture area, first fit over a list of free ranges (device offsets) */
#define MAXFREE 512
static struct range { unsigned long off, len; } fr[MAXFREE];
static int nfr = -1;

static unsigned long tex_alloc(unsigned long len)
{
    int i;
    unsigned long off;

    if (nfr < 0) {
        fr[0].off = TEX_OFF;
        fr[0].len = TEX_SIZE;
        nfr = 1;
    }
    len = (len + TEX_PAGE - 1) & ~(TEX_PAGE - 1);
    for (i = 0; i < nfr; i++)
        if (fr[i].len >= len) {
            off = fr[i].off;
            fr[i].off += len;
            fr[i].len -= len;
            if (fr[i].len == 0) {
                memmove(&fr[i], &fr[i + 1], (nfr - i - 1) * sizeof fr[0]);
                nfr--;
            }
            return off;
        }
    return 0;
}

static void tex_release(unsigned long off, unsigned long len)
{
    int i;

    len = (len + TEX_PAGE - 1) & ~(TEX_PAGE - 1);
    for (i = 0; i < nfr && fr[i].off < off; i++)
        ;
    /* merge with the neighbours where they touch */
    if (i > 0 && fr[i - 1].off + fr[i - 1].len == off) {
        fr[i - 1].len += len;
        if (i < nfr && off + len == fr[i].off) {
            fr[i - 1].len += fr[i].len;
            memmove(&fr[i], &fr[i + 1], (nfr - i - 1) * sizeof fr[0]);
            nfr--;
        }
        return;
    }
    if (i < nfr && off + len == fr[i].off) {
        fr[i].off = off;
        fr[i].len += len;
        return;
    }
    if (nfr == MAXFREE)
        return;                                 /* lost: too fragmented */
    memmove(&fr[i + 1], &fr[i], (nfr - i) * sizeof fr[0]);
    fr[i].off = off;
    fr[i].len = len;
    nfr++;
}

/* The texels go into board memory through one mapped window of the texture
   area, moved only when a texture falls outside it.  Mapping each upload
   (as before) costs the kernel a segment table -- two physically contiguous
   pages -- every time; hundreds of them per Quake III run helped exhaust
   the contiguous pages until hat_sdtalloc failed for the X server and the
   machine went down (2026-10-27).  The window stays small, as the process's
   address space is (16 MB by default). */
#define TEXWIN_SIZE  0x200000UL         /* 2 MB */
#define TEXWIN_ALIGN 0x10000UL
static char *texwin;
static unsigned long texwin_off, texwin_len;

static char *tex_window(unsigned long off, unsigned long bytes)
{
    unsigned long base, len;

    if (texwin && off >= texwin_off && off + bytes <= texwin_off + texwin_len)
        return texwin + (off - texwin_off);
    if (texwin)
        munmap((caddr_t) texwin, texwin_len);
    texwin = 0;
    base = off & ~(TEXWIN_ALIGN - 1);
    len = TEXWIN_SIZE;
    if (off + bytes - base > len)
        len = (off + bytes - base + TEXWIN_ALIGN - 1) & ~(TEXWIN_ALIGN - 1);
    if (base + len > TEX_OFF + TEX_SIZE)        /* stay inside the texture area */
        base = (TEX_OFF + TEX_SIZE - len) & ~(TEXWIN_ALIGN - 1);    /* still <= off */
    texwin = (char *) mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t) base);
    if (texwin == (char *) -1) {
        texwin = 0;
        return 0;
    }
    texwin_off = base;
    texwin_len = len;
    return texwin + (off - base);
}

static int tex_copy(struct s3d_tex *t, const unsigned char *texels)
{
    char *p;
#ifdef S3D_FAKE
    static char *fakemem;
    if (fake) {
        if (!fakemem)
            fakemem = (char *) malloc(TEX_SIZE);
        memcpy(fakemem + (t->off - TEX_OFF), texels, (size_t) t->w * t->h * t->bpp);
        return 0;
    }
#endif
    p = tex_window(t->off, t->bytes);
    if (!p)
        return -1;
    memcpy(p, texels, (size_t) t->w * t->h * t->bpp);
    return 0;
}

struct s3d_tex *s3d_tex_new(int w, int h, int bpp, const unsigned char *texels)
{
    struct s3d_tex *t;
    unsigned long i;

    if (!active || w < 1 || h < 1 || (bpp != 3 && bpp != 4))
        return 0;
    t = (struct s3d_tex *) calloc(1, sizeof *t);
    if (!t)
        return 0;
    t->w = w;
    t->h = h;
    t->bpp = bpp;
    t->bytes = ((unsigned long) w * h * bpp + TEX_PAGE - 1) & ~(TEX_PAGE - 1);
    t->off = tex_alloc(t->bytes);
    if (!t->off || tex_copy(t, texels) < 0) {
        if (t->off)
            tex_release(t->off, t->bytes);
        free(t);
        return 0;
    }
    s3d_flush();
    blk->offset[0] = sc; blk->offset[1] = board + FB_PHYS + t->off;
    blk->x[0] = w; blk->y[0] = h;
    blk->x[1] = bpp == 4 ? W3D_R8G8B8A8 : W3D_R8G8B8; blk->y[1] = 0;
    t->st = op(OP_CREATETEXTURE);
    if (!t->st) {
        tex_release(t->off, t->bytes);
        free(t);
        return 0;
    }
    /* recorded for the cleanup: a free slot, or a new one */
    for (i = 0; i < own->ntex && own->tex[i]; i++)
        ;
    if (i < MAXTEX) {
        own->tex[i] = t->st;
        if (i == own->ntex)
            own->ntex = i + 1;
        t->slot = (int) i;
    } else
        t->slot = -1;
    return t;
}

int s3d_tex_update(struct s3d_tex *t, const unsigned char *texels)
{
    if (!active || !t)
        return -1;
    s3d_finish();                               /* drawing may still read the old */
    st_texup++;
    if (tex_copy(t, texels) < 0)
        return -1;
    blk->offset[0] = sc; blk->offset[1] = t->st; blk->offset[2] = board + FB_PHYS + t->off;
    op(OP_UPDATETEXTURE);
    return 0;
}

void s3d_tex_free(struct s3d_tex *t)
{
    if (!t)
        return;
    if (active) {
        if (cur_valid && cur.tex == t)
            cur_valid = 0;
        if (want_valid && want.tex == t)
            want_valid = 0;
        s3d_finish();
        blk->offset[0] = sc; blk->offset[1] = t->st;
        op(OP_FREETEXTURE);
        if (t->slot >= 0 && own->tex[t->slot] == t->st)
            own->tex[t->slot] = 0;
        tex_release(t->off, t->bytes);
    }
    free(t);
}
