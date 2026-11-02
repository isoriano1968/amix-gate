/*
 * glbench.c -- phase 0 on the Z3660's core 1: osbench (Mesa 18.3, OSMesa +
 * softpipe) timed on the A9.  It plugs into h264bench's arm_main.c (control
 * block, reload, faults) in place of the H.264 decoder: the 68k posts a
 * "clip" of parameters with armbench and reads the usual results.
 *
 * The parameters, 32-bit big-endian words (tests/glbench-params.py):
 *   [0] "GLBN"  [1] scene (0 gears, 1 q3)  [2] width  [3] height
 *   [4] frames  [5] q3 triangles  [6] flags: 1 = GALLIUM_NOOP (only on the first job),
 *   2 = SOFTPIPE_NO_RAST (vertex work only), 4 = SOFTPIPE_NO_FAST,
 *   8 = calibration (cycles per load, divide, multiply-add), 16 = frames onto the overlay,
 *   32 = the last frame left at HB_DATA_OFF + 1 MB (glshow -grab), 64 = SOFTPIPE_FAST_NONEON,
 *   128 = SOFTPIPE_FAST_NOPAIR, 256 = PMU events 0x68/0x61, 512 = segment setup profile
 * Results: frames, width x height, ticks = the timed frames, checksum = the
 * low word of osbench's checksum (compare with the PC and the 060).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "bench.h"
#include "player.h"
#include "zzav.h"
#include "osbench.h"

/* ztop shows this name for core 1 */
const char hb_core_name[16] = "glbench";

/* zz_exec.c (in libOSMesa.a) reports to zzgl's server, which this program
   does not have: nothing to report to */
__attribute__((weak)) void zzp_present(const void *rgba, int w, int h, int again)
{
    (void)rgba; (void)w; (void)h; (void)again;
}
__attribute__((weak)) void zzp_refused(uint32_t what, uint32_t size)
{
    (void)what; (void)size;
}

/*
 * Start-up trace before main() (arm_main.c's W_STAGE starts there): the first
 * static constructor writes 0x11 into word 30 of the control block and pushes
 * it out of our L1, so the 68k sees how far a new image got.
 */
#define GLB_W_EARLY 30
#define GLB_W_PROF  32      /* sp_fast_prof[0..5] per frame (k cycles, counts) */
#define GLB_GRAB_OFF (HB_DATA_OFF + 0x100000UL)     /* flag 32: the last frame, RGBA */

extern uint64_t sp_fast_prof[16];
extern int sp_fast_evt_on;

static void show_frame(const void *buffer, int w, int h);

__attribute__((constructor(101))) static void glb_early(void)
{
    volatile uint32_t *w = (volatile uint32_t *)(0x18000000UL + HB_CTL_OFF) + GLB_W_EARLY;

    *w = __builtin_bswap32(0x11);
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 1\n"   /* DCCMVAC */
                     "dsb" : : "r"(w) : "memory");
}

static hb_clock clk;
static uint64_t tick_hz;

unsigned long long armgl_now_us(void);
unsigned long long armgl_now_us(void)
{
    extern uint64_t pl_tick_hz;
    extern uint64_t pl_ticks(void);

    return pl_ticks() * 1000000ULL / pl_tick_hz;
}

unsigned long long osb_now_us(void)
{
    return armgl_now_us();
}

static uint32_t word(const uint8_t *p, int i)
{
    return (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
}

int hb_decode(const uint8_t *data, size_t size, int flags, hb_clock clock,
              void (*progress)(uint32_t frames), struct hb_result *r)
{
    extern uint64_t pl_tick_hz;
    static int first = 1;
    char err[128];
    uint32_t w, h, frames;
    void *buf;
    double ms;

    (void)flags;
    (void)progress;
    clk = clock;
    tick_hz = pl_tick_hz;
    memset(r, 0, sizeof(*r));
    if (size < 28 || word(data, 0) != 0x474C424EUL) {
        r->errcode = -20;
        return -1;
    }
    w = word(data, 2);
    h = word(data, 3);
    frames = word(data, 4);
    if (word(data, 5))
        osb_q3_tris = (int)word(data, 5);
    if (first && (word(data, 6) & 1))
        setenv("GALLIUM_NOOP", "1", 1);
    first = 0;
    /* softpipe reads these when the context is created: per job */
    if (word(data, 6) & 2)
        setenv("SOFTPIPE_NO_RAST", "1", 1);
    else
        unsetenv("SOFTPIPE_NO_RAST");
    if (word(data, 6) & 4)
        setenv("SOFTPIPE_NO_FAST", "1", 1);
    else
        unsetenv("SOFTPIPE_NO_FAST");
    if (word(data, 6) & 64)
        setenv("SOFTPIPE_FAST_NONEON", "1", 1);
    else
        unsetenv("SOFTPIPE_FAST_NONEON");
    if (word(data, 6) & 128)
        setenv("SOFTPIPE_FAST_NOPAIR", "1", 1);
    else
        unsetenv("SOFTPIPE_FAST_NOPAIR");
    if (!w || !h || w > 2048 || h > 2048 || !frames || !(buf = calloc((size_t)w * h, 4))) {
        r->errcode = -21;
        return -1;
    }
    if (word(data, 6) & 8) {
        /* calibration: cycles per load (8 KB, 4 MB), per divide, per multiply-add */
        int i;
        volatile uint32_t *ctl = (volatile uint32_t *)(0x18000000UL + HB_CTL_OFF);
        static uint32_t small[2048];
        uint32_t *big = malloc(4u << 20), t0, sum = 0, k, res[4];
        volatile float fv = 1.0001f;
        float a = 1.0f, b = 0.0f;

        __asm__ volatile("mcr p15, 0, %0, c9, c12, 0" : : "r"(1u));
        __asm__ volatile("mcr p15, 0, %0, c9, c12, 1" : : "r"(1u << 31));
        for (k = 0; k < 2048; k++)
            small[k] = k;
        for (k = 0; k < (1u << 20); k++)
            big[k] = k;
        __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(t0));
        for (i = 0; i < 100; i++)
            for (k = 0; k < 2048; k++)
                sum += small[k];
        __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(res[0]));
        res[0] = (res[0] - t0) * 10 / (100 * 2048);
        __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(t0));
        for (k = 0; k < (1u << 20); k += 8)
            sum += big[k];
        __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(res[1]));
        res[1] = (res[1] - t0) * 10 / ((1u << 20) / 8);
        __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(t0));
        for (k = 0; k < 100000; k++)
            a = a / fv;
        __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(res[2]));
        res[2] = (res[2] - t0) * 10 / 100000;
        __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(t0));
        for (k = 0; k < 100000; k++)
            b = b * fv + 0.5f;
        __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(res[3]));
        res[3] = (res[3] - t0) * 10 / 100000;
        for (k = 0; k < 4; k++)
            ctl[GLB_W_PROF + k] = __builtin_bswap32(res[k]);   /* tenths of cycles */
        {
            /* what sp_fast.h's probes cost: a cycle counter read, an event
               counter read (select and read), tenths of cycles: words 37, 38 */
            uint32_t r5, r6, v = 0;

            __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(t0));
            for (k = 0; k < 10000; k++) {
                uint32_t c;

                __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(c));
                v += c;
            }
            __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(r5));
            r5 = (r5 - t0) * 10 / 10000;
            __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(t0));
            for (k = 0; k < 10000; k++) {
                uint32_t c;

                __asm__ volatile("mcr p15, 0, %1, c9, c12, 5\n"
                                 "mrc p15, 0, %0, c9, c13, 2" : "=r"(c) : "r"(k & 1));
                v += c;
            }
            __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(r6));
            r6 = (r6 - t0) * 10 / 10000;
            ctl[GLB_W_PROF + 5] = __builtin_bswap32(r5);
            ctl[GLB_W_PROF + 6] = __builtin_bswap32(r6);
            sum += v;
        }
        ctl[GLB_W_PROF + 4] = __builtin_bswap32(sum + (uint32_t)a + (uint32_t)b);
        free(big);
        free(buf);
        r->frames = 1;
        r->width = w;
        r->height = h;
        return 0;
    }
    osb_frame_hook = (word(data, 6) & 16) ? show_frame : NULL;
    /* the cycle counter for sp_fast's profile (sp_fast.h); with flag 256
       event counters 0 and 1 = instructions out of rename (0x68) and data
       cache stall cycles (0x61), else L1 data refills (0x03) */
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 0" : : "r"(1u));          /* PMCR.E */
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 5" : : "r"(0u));
    __asm__ volatile("mcr p15, 0, %0, c9, c13, 1" : : "r"(word(data, 6) & 256 ? 0x68u : 0x03u));
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 5" : : "r"(1u));
    __asm__ volatile("mcr p15, 0, %0, c9, c13, 1" : : "r"(0x61u));
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 1" : : "r"((1u << 31) | 3u));
    memset(sp_fast_prof, 0, sizeof(sp_fast_prof));     /* (all 16) */
    sp_fast_evt_on = (word(data, 6) & 256) != 0;     /* words 38, 39 */
    ms = osb_run(word(data, 1) == 3 ? OSB_Q3MT : word(data, 1) == 2 ? OSB_FLUID :
                 word(data, 1) ? OSB_Q3 : OSB_GEARS,
                 buf, (int)w, (int)h, (int)frames, err);
    {
        /* per frame, in thousands (cycles) or as counted: words 32..37 */
        volatile uint32_t *ctl = (volatile uint32_t *)(0x18000000UL + HB_CTL_OFF);
        int i;

        for (i = 0; i < 6; i++)
            ctl[GLB_W_PROF + i] = __builtin_bswap32((uint32_t)(sp_fast_prof[i] / (i < 4 ? 1000u : 1u) /
                                                               (frames + 1)));
        /* event counters 0 and 1 in the spans, per frame, thousands */
        ctl[GLB_W_PROF + 6] = __builtin_bswap32((uint32_t)(sp_fast_prof[8] / 1000u / (frames + 1)));
        ctl[GLB_W_PROF + 7] = __builtin_bswap32((uint32_t)(sp_fast_prof[14] / 1000u / (frames + 1)));
        if (word(data, 6) & 512) {
            /* instead: the perspective segments' setup, k cycles and count */
            ctl[GLB_W_PROF + 6] = __builtin_bswap32((uint32_t)(sp_fast_prof[12] / 1000u / (frames + 1)));
            ctl[GLB_W_PROF + 7] = __builtin_bswap32((uint32_t)(sp_fast_prof[13] / (frames + 1)));
        }
    }
    if (ms < 0) {
        free(buf);
        r->errcode = -22;
        return -1;
    }
    r->frames = frames;
    r->width = w;
    r->height = h;
    r->ticks = (uint64_t)(ms * frames * (double)tick_hz / 1000.0);
    r->checksum = (uint32_t)osb_checksum(buf, (int)w, (int)h);
    if (word(data, 6) & 32)     /* the last frame for glshow -grab (uncached) */
        memcpy((void *)(pl_board + GLB_GRAB_OFF), buf, (size_t)w * h * 4);
    free(buf);
    return 0;
}

/*
 * Flag 16: each frame onto the RTG overlay, as the video player does it
 * (player.c): glshow on the 68k switches the overlay on and fills in the
 * ZZAV_W_OVL_* words; the frame goes into the buffer not shown, upright
 * (GL's rows are bottom first) and as 0x00RRGGBB, then core 0 flips.
 */
static void show_frame(const void *buffer, int w, int h)
{
    volatile uint32_t *zz = (volatile uint32_t *)(pl_board + ZZAV_OFF);
    volatile uint32_t *c1ld = (volatile uint32_t *)(pl_board + C1LD_OFF);
    const uint32_t pitch = __builtin_bswap32(zz[ZZAV_W_OVL_PITCH]);
    const uint32_t row0 = __builtin_bswap32(zz[ZZAV_W_OVL_ROW0]);
    const int maxw = (int)__builtin_bswap32(zz[ZZAV_W_OVL_MAXW]);
    const int maxh = (int)__builtin_bswap32(zz[ZZAV_W_OVL_MAXH]);
    const int dbuf = zz[ZZAV_W_OVL_DBUF] != 0;
    static uint32_t line[2048];
    unsigned long long t0 = osb_now_us();
    uint32_t back;
    volatile uint8_t *dst;
    int x, y, k, sc = 1;

    /* a small picture: whole-number enlargement to the overlay's size */
    while (w * (sc + 1) <= maxw && h * (sc + 1) <= maxh && sc < 4)
        sc++;
    if (maxw < w)
        w = maxw;
    if (maxh < h)
        h = maxh;
    if (w <= 0 || h <= 0 || (uint32_t)(w * sc) * 4 > pitch)
        return;
    /* the last flip not taken yet: wait for it (at most 50 ms) */
    while (dbuf && c1ld[C1LD_W_FLIP] != 0 && osb_now_us() - t0 < 50000)
        ;
    back = !dbuf || __builtin_bswap32(c1ld[C1LD_W_FRONT]) == 2 ? ZZAV_OVL_OFF : ZZAV_OVL_OFF1;
    dst = pl_board + back + row0 * pitch;
    for (y = 0; y < h; y++) {
        const uint32_t *src = (const uint32_t *)buffer + (size_t)(h - 1 - y) * (size_t)w;

        for (x = 0; x < w; x++) {
            const uint32_t p = src[x];      /* r, g, b, a bytes */

            uint32_t q = (p & 0xff) << 16 | (p & 0xff00) | (p >> 16 & 0xff);

            q = q ? q : 0x010101;           /* 0 is transparent in the overlay */
            for (k = 0; k < sc; k++)
                line[x * sc + k] = q;
        }
        for (k = 0; k < sc; k++)
            memcpy((void *)(dst + (uint32_t)(y * sc + k) * pitch), line, (size_t)(w * sc) * 4);
    }
    if (dbuf)
        c1ld[C1LD_W_FLIP] = __builtin_bswap32(back == ZZAV_OVL_OFF ? 1 : 2);
}

/* what arm_main.c also calls: no video player and no FFmpeg in this image */
int player_run(void)
{
    volatile uint32_t *zz = (volatile uint32_t *)(pl_board + ZZAV_OFF);

    zz[ZZAV_W_ERR] = __builtin_bswap32(1);
    zz[ZZAV_W_STATE] = __builtin_bswap32(ZZAV_ERROR);   /* no player in this image */
    zz[ZZAV_W_CMD] = 0;
    return -1;
}

void av_log_set_level(int level)
{
    (void)level;
}
