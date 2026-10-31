/*
 * gpu.c -- z3660ARM_Core_gpu: zzgl's server on the Z3660's core 1 (see
 * zzgl/zzgl.h and docs/ARM-GL-PLAN.md).  It plugs into h264bench's
 * arm_main.c (control block, reload, faults): the 060's libzzgl posts a job
 * with the parameters "ZGLS", and the job is the server: GL commands from
 * the ring into Mesa 18.3 until ZZ_CLOSE, frames into the RTG overlay.
 *
 * While it runs it also publishes the board's HDMI sound position (the
 * audio formatter's transfer count) for the 060's sound code.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "bench.h"
#include "player.h"
#include "zzav.h"
#include "zzgl.h"
#include "zzgl_dec.h"

/* ztop shows this name for core 1 */
const char hb_core_name[16] = "gpu";

#define AUDIO_POS_REG   0x7FC00128UL    /* the formatter's position in the HDMI ring (vplay) */

static volatile uint32_t *blk;
static struct {
   uint32_t pitch, row0, maxw, maxh;
   int dbuf;
} ovl;

unsigned long long armgl_now_us(void);
unsigned long long armgl_now_us(void)
{
    return pl_ticks() * 1000000ULL / pl_tick_hz;
}

static void copy(void *dst, const void *src, size_t n)
{
    memcpy(dst, src, n);
}

static void publish_audio(void)
{
    blk[ZB_AUDIO_POS] = __builtin_bswap32(*(volatile uint32_t *)AUDIO_POS_REG);
    blk[ZB_AUDIO_SEQ] = __builtin_bswap32(__builtin_bswap32(blk[ZB_AUDIO_SEQ]) + 1);
}

static void overlay(uint32_t pitch, uint32_t row0, uint32_t maxw, uint32_t maxh, int dbuf)
{
    ovl.pitch = pitch;
    ovl.row0 = row0;
    ovl.maxw = maxw;
    ovl.maxh = maxh;
    ovl.dbuf = dbuf;
    blk[ZB_OVL] = __builtin_bswap32(pitch);
    blk[ZB_OVL + 1] = __builtin_bswap32(row0);
    blk[ZB_OVL + 2] = __builtin_bswap32(maxw);
    blk[ZB_OVL + 3] = __builtin_bswap32(maxh);
}

/*
 * The frame into the overlay as the video player does it (player.c): into
 * the buffer not shown, upright (GL's rows are bottom first), 0x00RRGGBB,
 * 0 made 0x010101 (0 is transparent there), then core 0 flips at vblank.
 */
extern uint64_t sp_fast_prof[16];

/* the last frame's profile into the block (zzglstat shows it) */
static void publish_prof(void)
{
    static unsigned long long last;
    const unsigned long long t = armgl_now_us();
    int i;

    for (i = 0; i < 8; i++) {
        const uint64_t v = sp_fast_prof[i] / (i == 4 || i == 5 || i == 6 ? 1u : 1000u);

        blk[ZB_PROF + i] = __builtin_bswap32((uint32_t)v);
        sp_fast_prof[i] = 0;
    }
    blk[ZB_PROF_EVT] = __builtin_bswap32((uint32_t)sp_fast_prof[8]);     /* L1 data refills in spans */
    blk[ZB_PROF_PATH] = __builtin_bswap32((uint32_t)sp_fast_prof[9]);
    blk[ZB_PROF_PATH + 1] = __builtin_bswap32((uint32_t)sp_fast_prof[10]);
    blk[ZB_PROF_PATH2] = __builtin_bswap32((uint32_t)sp_fast_prof[11]);
    blk[ZB_PROF_ROUTE] = __builtin_bswap32((uint32_t)sp_fast_prof[8]);
    blk[ZB_PROF_ROUTE + 1] = __builtin_bswap32((uint32_t)sp_fast_prof[15]);
    sp_fast_prof[15] = 0;
    blk[ZB_PROF_SEG] = __builtin_bswap32((uint32_t)(sp_fast_prof[12] / 1000));    /* k cycles */
    blk[ZB_PROF_SEG + 1] = __builtin_bswap32((uint32_t)sp_fast_prof[13]);
    sp_fast_prof[8] = sp_fast_prof[9] = sp_fast_prof[10] = sp_fast_prof[11] = 0;
    sp_fast_prof[12] = sp_fast_prof[13] = 0;
    if (last)
        blk[ZB_STATS_US] = __builtin_bswap32((uint32_t)(t - last));
    last = t;
}

static void present(const void *rgba, int w, int h)
{
    volatile uint32_t *c1ld = (volatile uint32_t *)(pl_board + C1LD_OFF);
    static uint32_t line[2048];
    const unsigned long long t0 = armgl_now_us();
    uint32_t back;
    volatile uint8_t *dst;
    int x, y;

    publish_audio();
    publish_prof();
    if (!ovl.pitch)
        return;
    if (w > (int)ovl.maxw)
        w = (int)ovl.maxw;
    if (h > (int)ovl.maxh)
        h = (int)ovl.maxh;
    if (w <= 0 || h <= 0 || w > 2048 || (uint32_t)w * 4 > ovl.pitch)
        return;
    /* the last flip not taken yet: wait for it (at most 50 ms) */
    while (ovl.dbuf && c1ld[C1LD_W_FLIP] != 0 && armgl_now_us() - t0 < 50000)
        ;
    back = !ovl.dbuf || __builtin_bswap32(c1ld[C1LD_W_FRONT]) == 2 ? ZZAV_OVL_OFF : ZZAV_OVL_OFF1;
    dst = pl_board + back + ovl.row0 * ovl.pitch;
    for (y = 0; y < h; y++) {
        const uint32_t *src = (const uint32_t *)rgba + (size_t)(h - 1 - y) * (size_t)w;

        for (x = 0; x < w; x++) {
            const uint32_t p = src[x];  /* r, g, b, a bytes */
            const uint32_t q = (p & 0xff) << 16 | (p & 0xff00) | (p >> 16 & 0xff);

            line[x] = q ? q : 0x010101;
        }
        memcpy((void *)(dst + (uint32_t)y * ovl.pitch), line, (size_t)w * 4);
    }
    if (ovl.dbuf)
        c1ld[C1LD_W_FLIP] = __builtin_bswap32(back == ZZAV_OVL_OFF ? 1 : 2);
}

/*
 * The sound: the 060 mixes into the stream (ZZGL_SND_OFF) at the byte
 * offset of its time; here the board's HDMI ring (160 ms, the formatter's
 * DMA reads it) is kept LEAD ahead of the DMA from the stream: swapped to
 * little-endian, doubled to 48 kHz when the stream runs at 24, silence
 * where the 060 has not written yet.  The DMA's position goes back to the
 * 060 in ZB_SND_POS (its clock), as in vplay's player.c.
 */
#define SND_PERIOD      3840                    /* bytes, 20 ms: the firmware's */
#define SND_LEAD        (SND_PERIOD * 6)        /* 120 ms */

static struct {
    int on, div;
    uint32_t dma_last;
    uint64_t dma_abs, wr_abs;                   /* HDMI bytes, multiples of the ring are its place */
    uint64_t start;                             /* dma_abs at time 0 */
    unsigned long long last_us;
} snd;

static inline uint32_t bget(int w) { return __builtin_bswap32(blk[w]); }
static inline void bput(int w, uint32_t v) { blk[w] = __builtin_bswap32(v); }

/* HDMI frames [abs, abs + n) (bytes) from the 060's ring into the HDMI ring */
static void snd_fill(uint64_t abs, uint32_t n)
{
    volatile uint32_t *hdmi = (volatile uint32_t *)(pl_board + ZZGL_HDMI_OFF);
    const volatile uint16_t *st = (const volatile uint16_t *)(pl_board + ZZGL_SND_OFF);
    const uint32_t frames = n / 4;
    uint32_t k;

    for (k = 0; k < frames; k++) {
        const uint64_t f = abs / 4 + k;                         /* HDMI frame */
        const uint64_t sf = (f - snd.start / 4) / (uint64_t)snd.div;    /* stream frame */
        const uint32_t i = (uint32_t)(sf % ZZGL_SND_FRAMES) * 2;
        int l = (int16_t)__builtin_bswap16(st[i]), r = (int16_t)__builtin_bswap16(st[i + 1]);

        if (snd.div == 2 && ((f - snd.start / 4) & 1)) {                          /* halfway to the next one */
            const uint32_t j = (uint32_t)((sf + 1) % ZZGL_SND_FRAMES) * 2;

            l = (l + (int16_t)__builtin_bswap16(st[j])) / 2;
            r = (r + (int16_t)__builtin_bswap16(st[j + 1])) / 2;
        }
        hdmi[(uint32_t)(f % (ZZGL_HDMI_SIZE / 4))] = (uint32_t)(uint16_t)l | (uint32_t)(uint16_t)r << 16;
    }
}

static void snd_service(void)
{
    const unsigned long long t = armgl_now_us();
    uint32_t pos;
    int div = (int)bget(ZB_SND_ON);

    if (div != 1 && div != 2) {
        snd.on = 0;
        return;
    }
    if (snd.on && t - snd.last_us < 5000)
        return;
    snd.last_us = t;
    pos = *(volatile uint32_t *)AUDIO_POS_REG & ~3u;
    if (pos >= ZZGL_HDMI_SIZE)
        pos = 0;
    if (!snd.on) {                              /* (re)started: time 0 is now */
        snd.on = 1;
        snd.div = div;
        snd.dma_last = pos;
        /* the DMA is somewhere in the ring already, core 0 started it: count
           from where it is, or the samples land beside what it plays */
        snd.dma_abs = snd.wr_abs = snd.start = pos;
        bput(ZB_SND_UNDER, 0);
    }
    snd.dma_abs += pos >= snd.dma_last ? pos - snd.dma_last : ZZGL_HDMI_SIZE - snd.dma_last + pos;
    snd.dma_last = pos;
    bput(ZB_SND_POS, (uint32_t)(snd.dma_abs - snd.start));
    if (snd.wr_abs < snd.dma_abs) {             /* the DMA overtook us */
        snd.wr_abs = snd.dma_abs;
        bput(ZB_SND_UNDER, bget(ZB_SND_UNDER) + 1);
    }
    if (snd.wr_abs < snd.dma_abs + SND_LEAD - SND_PERIOD / 2) {
        snd_fill(snd.wr_abs, (uint32_t)(snd.dma_abs + SND_LEAD - snd.wr_abs));
        snd.wr_abs = snd.dma_abs + SND_LEAD;
    }
}

static void tick(void)
{
    publish_audio();
    snd_service();
}

static void idle(void)
{
    static unsigned n;

    snd_service();
    if (++n % 64 == 0)
        publish_audio();
}

/*
 * Where core 1 is: a private timer interrupt once a second writes the
 * interrupted pc, lr, sp and the code addresses on the stack into the block.
 * Nothing else can see a core that spins without faulting.  Own vectors:
 * only the IRQ differs, and it saves no FP state, so samp_tick() uses none.
 */
#define PRIV(off)       (*(volatile uint32_t *)(0xF8F00000UL + (off)))
#define ICCICR          PRIV(0x100)
#define ICCPMR          PRIV(0x104)
#define ICCIAR          PRIV(0x10C)
#define ICCEOIR         PRIV(0x110)
#define PT_LOAD         PRIV(0x600)
#define PT_CTRL         PRIV(0x608)
#define PT_ISR          PRIV(0x60C)
#define ICDISER0        PRIV(0x1100)
#define ICDICER0        PRIV(0x1180)
#define ICDIPR_29       (*(volatile uint8_t *)0xF8F0141DUL)
#define PT_IRQ          29
#define PT_PRESCALE     255             /* 433 MHz / 256 */
#define PT_TICKS        1693000u        /* a second */

extern char __rodata_start[], _stack[];
extern void _vector_table(void);
extern void samp_vectors(void);

__asm__(".text\n"
        ".arm\n"
        ".balign 32\n"
        ".global samp_vectors\n"
        "samp_vectors:\n"
        "  b _vector_table+0\n"
        "  b _vector_table+4\n"
        "  b _vector_table+8\n"
        "  b _vector_table+12\n"
        "  b _vector_table+16\n"
        "  b _vector_table+20\n"
        "  b samp_irq\n"
        "  b _vector_table+28\n"
        "samp_irq:\n"
        "  sub lr, lr, #4\n"
        "  push {r0-r3, r12, lr}\n"
        "  mov r0, lr\n"
        "  mrs r3, cpsr\n"
        "  cps #0x1f\n"                /* the program's lr and sp */
        "  mov r1, lr\n"
        "  mov r2, sp\n"
        "  msr cpsr_c, r3\n"
        "  bl samp_tick\n"
        "  pop {r0-r3, r12, lr}\n"
        "  movs pc, lr\n");

__attribute__((used, target("general-regs-only"))) void samp_tick(uint32_t pc, uint32_t lr, uint32_t sp)
{
    const uint32_t id = ICCIAR;

    if ((id & 0x3ff) == PT_IRQ) {
        const uint32_t *w = (const uint32_t *)(sp & ~3u);
        const uint32_t *top = (const uint32_t *)_stack;
        int k = ZB_SAMPLE + 4, i;

        PT_ISR = 1;
        blk[ZB_SAMPLE + 1] = __builtin_bswap32(pc);
        blk[ZB_SAMPLE + 2] = __builtin_bswap32(lr);
        blk[ZB_SAMPLE + 3] = __builtin_bswap32(sp);
        for (i = 0; i < 4096 && w < top && k < ZB_REPLY; i++, w++)
            if (*w >= (uint32_t)_vector_table && *w < (uint32_t)__rodata_start)
                blk[k++] = __builtin_bswap32(*w);
        while (k < ZB_REPLY)
            blk[k++] = 0;
        blk[ZB_SAMPLE] = __builtin_bswap32(__builtin_bswap32(blk[ZB_SAMPLE]) + 1);
    }
    if ((id & 0x3ff) < 1020)
        ICCEOIR = id;
}

static void samp_start(void)
{
    __asm__ volatile("mcr p15, 0, %0, c12, c0, 0\n isb" : : "r"((uint32_t)samp_vectors));
    PT_CTRL = 0;
    PT_ISR = 1;
    PT_LOAD = PT_TICKS;
    ICDIPR_29 = 0xA0;
    ICDISER0 = 1u << PT_IRQ;
    ICCPMR = 0xF0;
    ICCICR = 1;
    PT_CTRL = PT_PRESCALE << 8 | 1 << 2 | 1 << 1 | 1;   /* interrupt, reload, on */
    __asm__ volatile("cpsie i");
}

static void samp_stop(void)
{
    __asm__ volatile("cpsid i");
    PT_CTRL = 0;
    PT_ISR = 1;
    ICDICER0 = 1u << PT_IRQ;
    __asm__ volatile("mcr p15, 0, %0, c12, c0, 0\n isb" : : "r"((uint32_t)_vector_table));
}

void spook_frame(uint32_t *fb, int w, int h, float time, float beat);

int hb_decode(const uint8_t *data, size_t size, int flags, hb_clock clock,
              void (*progress)(uint32_t frames), struct hb_result *r)
{
    struct zzgl_dec_env env;
    int ret;

    (void)flags;
    (void)clock;
    (void)progress;
    memset(r, 0, sizeof(*r));
    if (size < 4 || __builtin_bswap32(*(const uint32_t *)data) != ZZGL_JOB_MAGIC) {
        r->errcode = -30;
        return -1;
    }
    blk = (volatile uint32_t *)(pl_board + ZZGL_BLK_OFF);
    memset(&ovl, 0, sizeof(ovl));
    env.blk = blk;
    env.ring = (const uint8_t *)(pl_board + ZZGL_RING_OFF);
    env.reply = (uint8_t *)(pl_board + ZZGL_REPLY_OFF);
    env.copy_in = copy;
    env.copy_out = copy;
    env.present = present;
    env.overlay = overlay;
    env.idle = idle;
    env.tick = tick;
    env.effect = spook_frame;
    memset(&snd, 0, sizeof(snd));
    /* the cycle counter and event counter 0 = L1 data cache refills (0x03)
       for sp_fast.h's profile */
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 0" : : "r"(1u));
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 5" : : "r"(0u));            /* PMSELR = 0 */
    __asm__ volatile("mcr p15, 0, %0, c9, c13, 1" : : "r"(0x03u));         /* PMXEVTYPER */
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 1" : : "r"((1u << 31) | 1u));
    zzgl_dec_init(&env);
    samp_start();
    ret = zzgl_dec_run(&env, 0);
    samp_stop();
    r->frames = __builtin_bswap32(blk[ZB_FRAMES]);
    r->errcode = ret < 0 ? -31 : 0;
    zzgl_dec_shutdown();
    return ret < 0 ? -1 : 0;
}

/* what arm_main.c also calls: no video player and no FFmpeg in this image */
int player_run(void)
{
    volatile uint32_t *zz = (volatile uint32_t *)(pl_board + ZZAV_OFF);

    zz[ZZAV_W_ERR] = __builtin_bswap32(1);
    zz[ZZAV_W_STATE] = __builtin_bswap32(ZZAV_ERROR);
    zz[ZZAV_W_CMD] = 0;
    return -1;
}

void av_log_set_level(int level)
{
    (void)level;
}
