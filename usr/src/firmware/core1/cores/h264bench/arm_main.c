/*
 * arm_main.c -- h264bench on the Z3660's core 1 (bare metal, Xilinx
 * standalone BSP for ps7_cortexa9_1, as the firmware's 68040 emulator).
 *
 * Core 0 (patched firmware) starts this image at the address in its first
 * words when there is no emulator to run; it answers core 0's handshake
 * (0xFFFF0000 back to 0), then waits for jobs in the control block in board
 * memory: the 68k puts an Annex B clip at HB_DATA_OFF, its size and flags in
 * the block and HB_POSTED in W_STATE; core 1 copies the clip into its own
 * memory, decodes it with FFmpeg (timed by the global timer) and writes the
 * results back.  No interrupts, no UART output: core 0 owns both.
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include <malloc.h>
#include <sys/time.h>
#include "xil_mmu.h"
#include "xil_cache.h"
#include "xil_cache_l.h"
#include "xil_io.h"
#include "xil_exception.h"
#include "xtime_l.h"
#include <libavutil/log.h>
#include "bench.h"
#include "zzcrypto.h"
#include "zzav.h"
#include "core1_net.h"
#include "player.h"

#define RTG_BASE            0x18000000UL
#define CORE0_ACK           (*(volatile uint32_t *)0xFFFF0000)

/* as the emulator's defines.h */
#define WB                  0b01
#define WT                  0b10
#define OUTER               12
#define INNER               2
#define RTG_POLICY          (0x14DE2 | (WT << OUTER) | (WT << INNER))
#define FAULT_POLICY        0       /* no section: any access faults */

#define ARM_PLL_CTRL        0xF8000100
#define ARM_CLK_CTRL        0xF8000120

/* the image's first words: a mark and the entry, read by core 0 */
extern void _vector_table(void);
__attribute__((section(".benchid"), used))
static const uint32_t bench_id[2] = { 0x48323634, (uint32_t)_vector_table };

static volatile uint32_t *ctl = (volatile uint32_t *)(RTG_BASE + HB_CTL_OFF);

/* the name ztop shows; another program built on this file defines its own */
__attribute__((weak)) const char hb_core_name[16] = "h264bench";

/* zzcrypto's jobs from the 060 (zzc_arm.c, in the cores that have it):
   called while waiting, here and in the GL server and the player */
__attribute__((weak)) void hb_service(void)
{
}

/*
 * The heap.  Built with HB_HEAP_LOW it starts below the image, in core 1's
 * own DDR from 0x30000000 (the Z3660's memory map gives core 1
 * 0x30000000-0x40000000; the image must stay at C1LD_LOAD_ADDR, where core
 * 0's loader puts it), and goes on above the image (the linker's heap) when
 * that is full: about 245 MiB in all.  newlib's malloc takes the second
 * region as a new piece above the first; it would refuse one below.  Under
 * AmigaOS the SMUSH codecs use 0x30000000 too: no such core then.
 * Without HB_HEAP_LOW, only the linker's heap, as before.
 */
extern char _heap_start[], _heap_end[];
#define HEAP_LOW_START      0x30000000UL
#define HEAP_LOW_END        C1LD_LOAD_ADDR

static char *brk_cur, *brk_end;

void *_sbrk(ptrdiff_t incr);
void *_sbrk(ptrdiff_t incr)
{
    char *p;

    if (!brk_cur) {
#ifdef HB_HEAP_LOW
        brk_cur = (char *)HEAP_LOW_START;
        brk_end = (char *)HEAP_LOW_END;
#else
        brk_cur = _heap_start;
        brk_end = _heap_end;
#endif
    }
    if (incr > brk_end - brk_cur) {
        if (brk_end != _heap_end && incr <= _heap_end - _heap_start) {
            brk_cur = _heap_start;      /* the low region is full: the one above the image */
            brk_end = _heap_end;
        } else {
            errno = ENOMEM;
            return (void *)-1;
        }
    }
    p = brk_cur;
    brk_cur += incr;
    return p;
}

/* what malloc can still get: the region in use, the one above if not yet
   taken, and the free blocks inside the arena (zz_exec.c's reserve) */
size_t hb_heap_free(void);
size_t hb_heap_free(void)
{
    size_t n;

    if (!brk_cur)
        _sbrk(0);
    n = (size_t)(brk_end - brk_cur) + mallinfo().fordblks;
    if (brk_end != _heap_end)
        n += (size_t)(_heap_end - _heap_start);
    return n;
}

static uint32_t be(uint32_t v) { return __builtin_bswap32(v); }
static void put(int w, uint32_t v) { ctl[w] = be(v); }
static uint32_t get(int w) { return be(ctl[w]); }

static uint64_t ticks(void)
{
    XTime t;

    XTime_GetTime(&t);
    return t;
}

static uint32_t cpu_hz(void)
{
    uint32_t fbdiv = (Xil_In32(ARM_PLL_CTRL) >> 12) & 0x7F;
    uint32_t div = (Xil_In32(ARM_CLK_CTRL) >> 8) & 0x3F;

    return div ? (uint32_t)(33333333ULL * fbdiv / div) : 0;
}

/* player.c's platform */
volatile uint8_t *pl_board = (volatile uint8_t *)RTG_BASE;
/* the global timer runs at half the CPU clock: from the PLL at start-up
   (the Z3660's ARM speed is a setting: 666, 767, 867 MHz ...), not the
   BSP's COUNTS_PER_SECOND (666 MHz) */
uint64_t pl_tick_hz = COUNTS_PER_SECOND;
uint64_t pl_ticks(void) { return ticks(); }
#define AUDIO_XFER_COUNT    0x7FC00128UL    /* audio formatter MM2S transfer count: bytes into the ring */
uint32_t pl_audio_pos(void) { return Xil_In32(AUDIO_XFER_COUNT); }

/* newlib's gettimeofday (av_gettime): the global timer since start-up */
int _gettimeofday(struct timeval *tv, void *tz);
int _gettimeofday(struct timeval *tv, void *tz)
{
    uint64_t t = ticks();

    (void)tz;
    if (tv) {
        tv->tv_sec = (time_t)(t / pl_tick_hz);
        tv->tv_usec = (suseconds_t)(t % pl_tick_hz * 1000000 / pl_tick_hz);
    }
    return 0;
}

/* an exception: say so in the control block, then stop */
extern u32 DataAbortAddr, PrefetchAbortAddr, UndefinedExceptionAddr;

#define RELOAD_TRAMP        0x3FF00000UL
static void reload(uint32_t src, uint32_t size, uint32_t entry);

static void fault(void *kind)
{
    uint32_t far = 0, pc = 0;

    switch ((uint32_t)kind) {
    case XIL_EXCEPTION_ID_DATA_ABORT_INT:
        __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(far));     /* DFAR */
        pc = DataAbortAddr;
        break;
    case XIL_EXCEPTION_ID_PREFETCH_ABORT_INT:
        __asm__ volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(far));     /* IFAR */
        pc = PrefetchAbortAddr;
        break;
    default:
        pc = UndefinedExceptionAddr;
    }
    put(W_FAULT_ADDR, far);
    put(W_FAULT_PC, pc);
    put(W_FAULT, (uint32_t)kind);
    /* stopped, but still open to a reload (armbench -reload): no power
       cycle for a new image after a fault */
    put(W_STATE, HB_ERROR);
    for (;;)
        if (get(W_STATE) == HB_RELOAD) {
            const uint32_t src = get(W_DATA_OFF), size = get(W_DATA_SIZE), entry = get(W_FLAGS);
            const volatile uint32_t *img = (const volatile uint32_t *)(RTG_BASE + src);

            if (src == HB_DATA_OFF && size >= 8 && size <= HB_IMAGE_MAX && img[0] == 0x48323634 &&
                entry >= C1LD_LOAD_ADDR && entry < C1LD_LOAD_ADDR + size && !(entry & 3) &&
                C1LD_LOAD_ADDR + size <= RELOAD_TRAMP) {
                put(W_FAULT, 0);
                reload(src, size, entry);       /* does not return */
            }
            put(W_STATE, HB_ERROR);
        }
}

/* load: idle time measured in the waiting loop, per one-second window */
static uint64_t win_start, win_idle;

static void load_window(uint64_t now)
{
    uint64_t span = now - win_start;

    if (span < pl_tick_hz)
        return;
    put(W_LOAD, (uint32_t)(1000 - (win_idle > span ? span : win_idle) * 1000 / span));
    win_start = now;
    win_idle = 0;
}

/* from the player, which is never in the waiting loop */
void pl_load(uint64_t idle, uint32_t frames)
{
    win_idle += idle;
    put(W_PROGRESS, frames);
    load_window(ticks());
}

static void progress(uint32_t frames)
{
    put(W_PROGRESS, frames);
    load_window(ticks());       /* no idle time while decoding */
}

/*
 * Change the translation table without Xil_SetTlbAttributes: that flushes the
 * whole L2 by way on every call, and core 0 does the same during SCSI I/O
 * (scsi.c Xil_L2CacheFlush) -- two background operations on the L2
 * controller at once.  Here only our own L1 is cleaned; the table walk reads
 * the L2.
 */
extern u32 MMUTable[];

static void set_section(uint32_t addr, uint32_t attr)
{
    MMUTable[addr >> 20] = attr ? (addr & 0xFFF00000UL) | attr : 0;
}

static void mmu_sync(void)
{
    Xil_L1DCacheFlush();
    __asm__ volatile("dsb\n"
                     "mcr p15, 0, %0, c8, c7, 0\n"     /* TLBIALL */
                     "mcr p15, 0, %0, c7, c5, 6\n"     /* BPIALL */
                     "dsb\n"
                     "isb" : : "r"(0) : "memory");
}

/*
 * Reload: copy a new image over this one and start it, without a reset.
 * The copy runs from RELOAD_TRAMP (uncached, above image, heap and stack):
 *   1. copy through the cache (the shared L2 then holds the new contents),
 *   2. clean our L1 into the L2 (set/way: our own L1 only),
 *   3. MMU and caches off, copy again straight into DDR (what the new
 *      image's boot.S reads before it turns the MMU on),
 *   4. invalidate I-cache, branch predictor, TLB; branch to the entry.
 * No L2 maintenance: whatever the L2 writes back later is the new contents
 * already (core 0's whole-L2 flushes during SCSI I/O make L2 operations
 * from here unsafe, see set_section).
 */
#define RELOAD_TTB          (RELOAD_TRAMP + 0x4000UL)  /* 16 KB aligned page table copy */

static inline uint32_t ttbr0(void)
{
    uint32_t v;

    __asm__ volatile("mrc p15, 0, %0, c2, c0, 0" : "=r"(v));
    return v;
}

extern void reload_tramp(uint32_t dst, uint32_t src, uint32_t size, uint32_t entry);
extern char reload_tramp_end[];
__asm__(
    ".text\n.arm\n.align 5\n.global reload_tramp\n"
    "reload_tramp:\n"
    "  mov r4, r0\n  mov r5, r1\n  mov r6, r2\n"
    "1: ldr r7, [r1], #4\n  str r7, [r0], #4\n  subs r2, r2, #4\n  bgt 1b\n"
    "  dsb\n"
    "  mov r8, #0\n"                                    /* way << 30 */
    "2: mov r9, #0\n"                                   /* set << 5: 256 sets of 32 bytes */
    "3: orr r10, r9, r8\n  mcr p15, 0, r10, c7, c14, 2\n"  /* DCCISW */
    "  add r9, r9, #32\n  cmp r9, #0x2000\n  blt 3b\n"
    "  adds r8, r8, #0x40000000\n  bne 2b\n"            /* 4 ways */
    "  dsb\n"
    "  mrc p15, 0, r10, c1, c0, 0\n"
    "  bic r10, r10, #0x1\n  bic r10, r10, #0x4\n  bic r10, r10, #0x1000\n"  /* M, C, I off */
    "  mcr p15, 0, r10, c1, c0, 0\n  isb\n"
    "  mov r0, r4\n  mov r1, r5\n  mov r2, r6\n"
    "4: ldr r7, [r1], #4\n  str r7, [r0], #4\n  subs r2, r2, #4\n  bgt 4b\n"
    "  dsb\n  mov r10, #0\n"
    "  mcr p15, 0, r10, c7, c5, 0\n"                   /* ICIALLU */
    "  mcr p15, 0, r10, c7, c5, 6\n"                   /* BPIALL */
    "  mcr p15, 0, r10, c8, c7, 0\n"                   /* TLBIALL */
    "  dsb\n  isb\n"
    "  bx r3\n"
    ".global reload_tramp_end\nreload_tramp_end:\n");

static void reload(uint32_t src, uint32_t size, uint32_t entry)
{
    uint32_t n = (uint32_t)(reload_tramp_end - (char *)reload_tramp);
    void (*tramp)(uint32_t, uint32_t, uint32_t, uint32_t) = (void (*)(uint32_t, uint32_t, uint32_t, uint32_t))RELOAD_TRAMP;

    put(W_MAGIC, 0);                    /* the 68k waits for the new image's mark */
    put(W_STAGE, 0);
    ((volatile uint32_t *)(RTG_BASE + ZC_BLK_OFF))[ZC_W_MAGIC] = 0;   /* no jobs until the new image says */
    ((volatile uint32_t *)(RTG_BASE + ZZAV_OFF))[ZZAV_W_MAGIC] = 0;
    memcpy((void *)RELOAD_TRAMP, (const void *)reload_tramp, n);   /* uncached: straight to DDR */
    /* the copy overwrites our own page table (MMUTable, inside the image) while
       the MMU is still on: a table walk would then read the new image's bytes.
       Walk a copy in the trampoline's megabyte instead (uncached walks). */
    memcpy((void *)RELOAD_TTB, (const void *)(ttbr0() & ~0x3FFFUL), 0x4000);
    __asm__ volatile("dsb\n"
                     "mcr p15, 0, %0, c2, c0, 0\n"     /* TTBR0 */
                     "isb\n"
                     "mcr p15, 0, %1, c8, c7, 0\n"     /* TLBIALL */
                     "dsb\n"
                     "isb" : : "r"(RELOAD_TTB), "r"(0) : "memory");
    __asm__ volatile("dsb\n"
                     "mcr p15, 0, %0, c7, c5, 0\n"     /* ICIALLU */
                     "mcr p15, 0, %0, c7, c5, 6\n"     /* BPIALL */
                     "isb" : : "r"(0) : "memory");
    tramp(C1LD_LOAD_ADDR, RTG_BASE + src, (size + 3) & ~3UL, entry);
}

/* the same for a core whose main loop no longer looks: gpu.c's timer calls it.
   Returns if nothing is posted, or not an image */
void reload_posted(void)
{
    const uint32_t src = get(W_DATA_OFF), size = get(W_DATA_SIZE), entry = get(W_FLAGS);
    const volatile uint32_t *img = (const volatile uint32_t *)(RTG_BASE + src);

    if (get(W_STATE) == HB_RELOAD && src == HB_DATA_OFF && size >= 8 && size <= HB_IMAGE_MAX &&
        img[0] == 0x48323634 && entry >= C1LD_LOAD_ADDR && entry < C1LD_LOAD_ADDR + size &&
        !(entry & 3) && C1LD_LOAD_ADDR + size <= RELOAD_TRAMP) {
        put(W_STATE, HB_IDLE);
        reload(src, size, entry);       /* does not return */
    }
}

int main(void)
{
    struct hb_result r;
    uint32_t beat = 0;
    uint8_t *clip = NULL;
    int i;

    /* the control block first, uncached, so each stage below shows on the 68k */
    set_section(RTG_BASE + HB_CTL_OFF, NORM_NONCACHE);
    mmu_sync();
    put(W_STAGE, 1);
    put(W_FAULT, 0);
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_DATA_ABORT_INT, fault, (void *)XIL_EXCEPTION_ID_DATA_ABORT_INT);
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_PREFETCH_ABORT_INT, fault,
                                 (void *)XIL_EXCEPTION_ID_PREFETCH_ABORT_INT);
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_UNDEFINED_INT, fault, (void *)XIL_EXCEPTION_ID_UNDEFINED_INT);
    put(W_STAGE, 2);
    Xil_ICacheEnable();
    Xil_L1DCacheEnable();       /* the L2 is core 0's, already on */
    put(W_STAGE, 3);
    /* keep off the 060's RAM (and Zorro III RAM) and core 0's firmware:
       a stray pointer faults instead of writing there */
    for (i = 0; i < 0x180; i++)      /* from 0: a NULL pointer faults too */
        set_section(i * 0x100000UL, FAULT_POLICY);
    for (i = 0x200; i < 0x300; i++)
        set_section(i * 0x100000UL, FAULT_POLICY);
    for (i = 0x182; i < 0x1FE; i++)      /* board memory, as the emulator maps it */
        set_section(i * 0x100000UL, RTG_POLICY);
    /* but what the 68k shares with us uncached: our cache would keep a stale copy */
    set_section(RTG_BASE + HB_CTL_OFF, NORM_NONCACHE);
    for (i = 0; i < (int)(HB_IMAGE_MAX >> 20); i++)   /* a reload reads the image from it */
        set_section(RTG_BASE + HB_DATA_OFF + i * 0x100000UL, NORM_NONCACHE);
    for (i = 0; i < (int)(ZZAV_AREA_SIZE >> 20); i++)    /* the player's packet ring */
        set_section(RTG_BASE + ZZAV_RING_OFF + i * 0x100000UL, NORM_NONCACHE);
    set_section(RTG_BASE + ZZAV_AUDIO_OFF, NORM_NONCACHE);  /* the board's sound ring: the DMA reads DDR */
    set_section(RTG_BASE + ZZAV_PROBE_OFF, NORM_NONCACHE); /* same megabyte as the blocks: already */
    set_section(RTG_BASE + C1NET_OFF, NORM_NONCACHE);   /* our network rings: core 0 writes them */
    set_section(RELOAD_TRAMP, NORM_NONCACHE);   /* reload(): its code runs on with the MMU off */
    set_section(0xFFF00000UL, NORM_NONCACHE);     /* the handshake word */
    mmu_sync();
    put(W_STAGE, 4);
    av_log_set_level(AV_LOG_QUIET);     /* av_log would write to core 0's UART */

    for (i = 0; i < 4; i++)              /* big-endian words: the 68k reads the bytes in order */
        put(W_NAME + i, (uint32_t)hb_core_name[i * 4] << 24 | (uint32_t)hb_core_name[i * 4 + 1] << 16 |
                        (uint32_t)hb_core_name[i * 4 + 2] << 8 | (uint32_t)hb_core_name[i * 4 + 3]);
    put(W_STATE, HB_IDLE);
    put(W_LOAD, 0);
    win_start = ticks();
    if (cpu_hz())
        pl_tick_hz = cpu_hz() / 2;
    put(W_CPU_HZ, cpu_hz());
    put(W_TICK_HZ, pl_tick_hz);
    ((volatile uint32_t *)(RTG_BASE + ZZAV_OFF))[ZZAV_W_CMD] = 0;
    ((volatile uint32_t *)(RTG_BASE + ZZAV_OFF))[ZZAV_W_STATE] = 0;
    ((volatile uint32_t *)(RTG_BASE + ZZAV_OFF))[ZZAV_W_MAGIC] = __builtin_bswap32(ZZAV_MAGIC);
    ((volatile uint32_t *)(RTG_BASE + ZC_BLK_OFF))[ZC_W_MAGIC] = 0;   /* a core without zzcrypto */
    put(W_MAGIC, HB_MAGIC_READY);
    put(W_STAGE, 5);
    CORE0_ACK = 0;

    for (;;) {
        uint32_t size, flags;
        int ret;

        if (__builtin_bswap32(((volatile uint32_t *)(RTG_BASE + ZZAV_OFF))[ZZAV_W_CMD]) == ZZAV_CMD_OPEN) {
            put(W_STATE, HB_RUNNING);
            put(W_PROGRESS, 0);
            win_start = ticks();
            win_idle = 0;
            player_run();
            put(W_STATE, HB_IDLE);
            win_start = ticks();
            win_idle = 0;
            continue;
        }
        if (__builtin_bswap32(((volatile uint32_t *)(RTG_BASE + ZZAV_OFF))[ZZAV_W_CMD]) == ZZAV_CMD_PROBE) {
            volatile uint32_t *pr = (volatile uint32_t *)(RTG_BASE + ZZAV_PROBE_OFF);
            uint64_t tp = ticks();

            pr[0] = 0;
            for (i = 0; i < ZZAV_PROBE_N; i++) {
                pr[8 + i] = __builtin_bswap32(Xil_In32(0x7FC00128UL));
                while (ticks() - tp < (uint64_t)(i + 1) * (pl_tick_hz / 2000))
                    ;
            }
            pr[1] = __builtin_bswap32(ZZAV_PROBE_N);
            pr[2] = __builtin_bswap32(500);
            pr[3] = __builtin_bswap32(Xil_In32(0x7FC00110UL));     /* MM2S CTRL */
            pr[4] = __builtin_bswap32(Xil_In32(0x7FC00114UL));     /* STS */
            pr[5] = __builtin_bswap32(Xil_In32(0x7FC0011CUL));     /* PERIOD_CONFIG */
            pr[6] = __builtin_bswap32(Xil_In32(0x7FC00120UL));     /* BUFF_ADDR */
            pr[7] = __builtin_bswap32(Xil_In32(0x7FC00144UL));     /* BYTES_PER_CH */
            pr[0] = __builtin_bswap32(0x50524F42);
            ((volatile uint32_t *)(RTG_BASE + ZZAV_OFF))[ZZAV_W_CMD] = 0;
            continue;
        }
        if (get(W_STATE) == HB_RELOAD) {
            uint32_t src = get(W_DATA_OFF), size = get(W_DATA_SIZE), entry = get(W_FLAGS);
            const volatile uint32_t *img = (const volatile uint32_t *)(RTG_BASE + src);

            if (src == HB_DATA_OFF && size >= 8 && size <= HB_IMAGE_MAX && img[0] == 0x48323634 &&
                entry >= C1LD_LOAD_ADDR && entry < C1LD_LOAD_ADDR + size && !(entry & 3) &&
                C1LD_LOAD_ADDR + size <= RELOAD_TRAMP) {
                put(W_STATE, HB_IDLE);
                reload(src, size, entry);       /* does not return */
            }
            put(W_ERRCODE, (uint32_t)-10);      /* refused: not an image */
            put(W_STATE, HB_ERROR);
            continue;
        }
        if (get(W_STATE) != HB_POSTED) {
            uint64_t t0 = ticks(), t1;

            put(W_HEARTBEAT, ++beat);
            put(W_NOW, (uint32_t)t0);
            hb_service();
            /* ~1 ms; no WFE/WFI: nothing is sure to wake core 1 (no interrupts
               of its own, no event stream on the A9, core 0's SEV is rare) */
            while (ticks() - t0 < pl_tick_hz / 1000)
                __asm__ volatile("nop");
            t1 = ticks();
            win_idle += t1 - t0;
            load_window(t1);
            continue;
        }
        put(W_STATE, HB_RUNNING);
        put(W_PROGRESS, 0);
        size = get(W_DATA_SIZE);
        flags = get(W_FLAGS);
        put(W_CPU_HZ, cpu_hz());
        if (size == 0 || size > HB_DATA_MAX || get(W_DATA_OFF) != HB_DATA_OFF) {
            put(W_ERRCODE, (uint32_t)-1);
            put(W_STATE, HB_ERROR);
            continue;
        }
        free(clip);
        clip = malloc(size + 64);       /* the parser may read a little past the end */
        if (!clip) {
            put(W_ERRCODE, (uint32_t)-2);
            put(W_STATE, HB_ERROR);
            continue;
        }
        memcpy(clip, (const void *)(RTG_BASE + HB_DATA_OFF), size);
        memset(clip + size, 0, 64);
        ret = hb_decode(clip, size, (int)flags, ticks, progress, &r);
        put(W_FRAMES, r.frames);
        put(W_WIDTH, r.width);
        put(W_HEIGHT, r.height);
        put(W_TICKS_LO, (uint32_t)r.ticks);
        put(W_TICKS_HI, (uint32_t)(r.ticks >> 32));
        put(W_MAX_TICKS, (uint32_t)r.max_ticks);
        put(W_CHECKSUM, r.checksum);
        put(W_ERRORS, r.errors);
        put(W_ERRCODE, (uint32_t)r.errcode);
        put(W_STATE, ret < 0 ? HB_ERROR : HB_DONE);
        load_window(ticks());
    }
}
