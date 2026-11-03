/*
 * bench.h -- h264bench: FFmpeg's H.264 decoder on the Z3660's second ARM
 * core, timed.  The control block is shared by the 68k (tests/armbench.c),
 * core 1 (arm_main.c) and the PC (host_main.c, reference checksums).
 *
 * Board offsets are from the start of the Z3660's board memory: the ARM sees
 * them at RTG_BASE (0x18000000) + offset, the 68k through /dev/zz9000.
 */
#ifndef BENCH_H
#define BENCH_H

#include <stddef.h>
#include <stdint.h>

/*
 * Core 0 serves the 68k's accesses to board memory through its own caches
 * (rtg_cache_policy_core0), and core 1 does not see core 0's L1: shared
 * data must lie where core 0 does not hold it back.
 * The control block: the last page of the MPEG FIFO's megabyte (0x07A00000,
 * uncached on core 0; free while no MPEG-1 file plays on the ARM).
 */
#define HB_CTL_OFF          0x07AFF000UL
/* the clip: the overlay's second half (write-through on core 0; free while
   the overlay buffer at 0x06000000 is under 8 MB, as at 1920x1080) */
#define HB_DATA_OFF         0x06800000UL
#define HB_DATA_MAX         0x00800000UL
/* a core image for -load/-reload: the data window and what follows it up to
   0x07400000, core 0's MPEG sound (0x07000000) and the start of the player's
   and GL server's ring (0x07100000): nothing uses them while core 1 is idle,
   and a reload happens only then.  The amix core is 10.3 MB; a core with an
   older harness takes 9 MiB (until 2026-11-03) or 8 MiB.  Job data stays
   within HB_DATA_MAX. */
#define HB_IMAGE_MAX        0x00C00000UL

/*
 * Core 0's loader (patch to the Z3660 firmware's main.c): one page below the
 * control block.  The 68k puts a core 1 image (h264bench.bin, little-endian ARM
 * code) somewhere in board memory, fills the block and writes C1LD_CMD_LOAD;
 * core 0 copies the image to C1LD_W_ADDR and starts core 1 at C1LD_W_ENTRY
 * (0xFFFFFFF0 + SEV, as the emulator), then clears C1LD_W_CMD.  Once per power
 * cycle: core 1 is never reset (an SLCR reset of core 1 froze core 0).
 */
#define C1LD_OFF            0x07AFE000UL
#define C1LD_MAGIC          0x43314C44UL    /* "C1LD": core 0 has the loader */
#define C1LD_CMD_LOAD       0x4C4F4144UL    /* "LOAD" */
#define C1LD_CMD_STOP       0x53544F50UL    /* "STOP": refused (-5) by loader v2 */
enum { C1LD_W_MAGIC, C1LD_W_CMD, C1LD_W_SRC, C1LD_W_SIZE, C1LD_W_ADDR, C1LD_W_ENTRY,
       C1LD_W_STATUS, C1LD_W_COUNT,         /* status: 1 started, < 0 refused */
       C1LD_W_FEAT,                         /* what core 0 does (0 on older firmware): */
       C1LD_W_FLIP,                         /* core 1: 1/2 = show overlay buffer 0/1 from the next vblank */
       C1LD_W_FRONT };                      /* core 0: the overlay buffer shown (1/2) */
#define C1LD_FEAT_OVL_FETCH 1               /* the overlay is 1:1 at 1080p (no doubled rows) */
#define C1LD_FEAT_OVL_FLIP  2               /* two overlay buffers, flipped at vblank */
#define C1LD_LOAD_ADDR      0x38000000UL    /* lscript.ld */

#define HB_MAGIC_READY      0x48425244UL    /* "HBRD": core 1 runs and waits */

/* W_STATE; HB_RELOAD: the 68k has put a new core 1 image (h264bench.bin) at
   W_DATA_OFF, W_DATA_SIZE bytes, entry in W_FLAGS: core 1 copies it over
   itself and starts it (no reset, no power cycle) */
enum { HB_IDLE, HB_POSTED, HB_RUNNING, HB_DONE, HB_ERROR, HB_RELOAD };

/* W_FLAGS */
#define HB_FLAG_NODEBLOCK   1               /* skip the loop filter */

/* 32-bit words of the control block, big-endian (the 68k's order) */
enum {
    W_MAGIC, W_STATE, W_SEQ,
    W_DATA_OFF, W_DATA_SIZE, W_FLAGS,           /* set by the 68k */
    W_FRAMES, W_WIDTH, W_HEIGHT,                /* results */
    W_TICKS_LO, W_TICKS_HI, W_MAX_TICKS,        /* decoding only, in timer ticks */
    W_CHECKSUM, W_ERRORS, W_ERRCODE,
    W_CPU_HZ, W_TICK_HZ,                        /* ARM clock (from the PLL), timer rate */
    W_HEARTBEAT, W_PROGRESS,                    /* alive while idle, frames while running */
    W_NOW,                                      /* the timer (low word) at the last heartbeat */
    W_FAULT, W_FAULT_ADDR, W_FAULT_PC,          /* an exception on core 1: kind (1 undefined, 3 prefetch,
                                                   4 data abort), DFAR/IFAR, the instruction */
    W_LOAD,                                     /* core 1's load over the last second, per mille
                                                   (read by ztop on AMIX) */
    W_STAGE,                                    /* how far core 1's start-up got (1-4, 5 = ready) */
    W_NAME, W_NAME_END = W_NAME + 3,            /* the program's name, 16 bytes, NUL-padded (ztop) */
    W_NWORDS
};

struct hb_result {
    uint32_t frames, width, height, errors, checksum;
    uint64_t ticks, max_ticks;
    int errcode;
};

typedef uint64_t (*hb_clock)(void);

/* decode the Annex B stream data[0..size); progress, if not NULL, counts
   frames as they come (in the board's byte order: set by the caller) */
int hb_decode(const uint8_t *data, size_t size, int flags, hb_clock clock,
              void (*progress)(uint32_t frames), struct hb_result *r);

#endif
