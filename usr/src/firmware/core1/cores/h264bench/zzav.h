/*
 * zzav.h -- playing a file with the Z3660's second ARM core: the 68060
 * (vplay, src/zzav.c) demuxes and hands the packets over, core 1
 * (player.c) decodes H.264 and AAC with FFmpeg, shows the picture in the
 * RTG's overlay and plays the sound on the board's HDMI.
 *
 * Board offsets are from the start of the Z3660's board memory, as in
 * bench.h.  Words are 32-bit big-endian (the 68k's order); packet data is
 * bytes as the demuxer gave them.
 */
#ifndef ZZAV_H
#define ZZAV_H

/* the block: one page below core 0's loader, uncached on both sides */
#define ZZAV_OFF            0x07AFD000UL
#define ZZAV_MAGIC          0x5A5A4156UL    /* "ZZAV": core 1's player waits for OPEN */

/* the packet ring and the codecs' extradata: 8 MB the firmware does not use
   (between the MPEG sound at 0x07000000 and its FIFO at 0x07A00000), uncached
   on core 1; 0x06800000 is the overlay's second buffer */
#define ZZAV_RING_OFF       0x07100000UL
#define ZZAV_RING_SIZE      0x007C0000UL
#define ZZAV_VEXTRA_OFF     0x078C0000UL
#define ZZAV_AEXTRA_OFF     0x078E0000UL
#define ZZAV_AREA_SIZE      0x00800000UL
#define ZZAV_EXTRA_MAX      0x00020000UL

/* the overlay buffer (firmware: 32-bit BGRA, one row per screen line, the
   screen's width) and the board's sound ring (8 periods of 20 ms, 48 kHz
   16-bit stereo little-endian, played round and round by the audio DMA) */
#define ZZAV_OVL_OFF        0x06000000UL
#define ZZAV_OVL_OFF1       0x06800000UL    /* the second buffer, with C1LD_FEAT_OVL_FLIP */
#define ZZAV_AUDIO_OFF      0x07CE0000UL
#define ZZAV_AUDIO_SIZE     (3840UL * 8)

/* ZZAV_W_CMD, written by the 68k; core 1 sets it back to 0 when taken */
enum { ZZAV_CMD_NONE, ZZAV_CMD_OPEN, ZZAV_CMD_CLOSE,
       ZZAV_CMD_PROBE };                /* idle only: sample the audio formatter into ZZAV_PROBE_OFF */
/* ZZAV_W_STATE */
enum { ZZAV_IDLE, ZZAV_PLAYING, ZZAV_FINISHED, ZZAV_ERROR };
/* ZZAV_W_ACODEC / ZZAV_W_VCODEC */
enum { ZZAV_NONE, ZZAV_H264, ZZAV_AAC };

enum {
    ZZAV_W_MAGIC, ZZAV_W_CMD, ZZAV_W_STATE, ZZAV_W_ERR,
    ZZAV_W_WR, ZZAV_W_RD,                       /* ring indices in bytes: 68k writes, core 1 reads */
    ZZAV_W_VCODEC, ZZAV_W_VEXTRA_LEN, ZZAV_W_VTB_NUM, ZZAV_W_VTB_DEN, ZZAV_W_VW, ZZAV_W_VH,
    ZZAV_W_ACODEC, ZZAV_W_AEXTRA_LEN, ZZAV_W_ATB_NUM, ZZAV_W_ATB_DEN, ZZAV_W_ARATE, ZZAV_W_ACH,
    ZZAV_W_OVL_PITCH,                           /* bytes per overlay row (the screen's width * 4) */
    ZZAV_W_OVL_ROW0,                            /* the overlay row the picture's top goes to */
    ZZAV_W_OVL_SHIFT,                           /* 0: 1:1, 1: half size */
    ZZAV_W_OVL_MAXW, ZZAV_W_OVL_MAXH,           /* crop to this (after the shift) */
    ZZAV_W_PAUSE,                               /* 68k: 1 holds the clock and the sound */
    ZZAV_W_VOLUME,                              /* 68k: 0..256 (256 = as decoded) */
    ZZAV_W_EOF,                                 /* 68k: 1 when the last packet is in the ring */
    ZZAV_W_OVL_DBUF,                            /* 68k: 1 if core 0 flips the overlay's two buffers */
    ZZAV_W_OVL_SCALE,                           /* 68k: 1..4: each pixel shown that many times (no shift) */
    /* core 1's report */
    ZZAV_W_CLOCK_MS,                            /* media time shown/heard now, ms */
    ZZAV_W_SHOWN, ZZAV_W_DROPPED, ZZAV_W_DECODED,
    ZZAV_W_UNDERRUNS,                           /* sound periods filled with silence */
    ZZAV_W_VDEC_US,                             /* the last second's average decode time per frame */
    ZZAV_W_ALEAD_MS,                            /* sound written ahead of the DMA */
    ZZAV_W_OUTW, ZZAV_W_OUTH,                   /* the picture as written into the overlay */
    ZZAV_W_LAG_MS,                              /* how late the last frame was (signed) */
    ZZAV_W_SKIP,                                /* the decoder's skip level (0 none, 1 no non-reference
                                                   frames, 2 + no loop filter, 3 keyframes only) */
    /* diagnosis */
    ZZAV_W_APKTS, ZZAV_W_AFRAMES,               /* sound packets taken from the ring, frames decoded */
    ZZAV_W_ASAMPLES,                            /* 48 kHz frames resampled */
    ZZAV_W_AERR,                                /* the last error from the sound decoder (signed) */
    ZZAV_W_DMA_RAW,                             /* the audio formatter's transfer count, as last read */
    ZZAV_W_DMA_JUMPS,                           /* reads that went backwards (ring wraps, or worse) */
    ZZAV_W_CLOCKSRC,                            /* 1 the sound, 2 the timer (sound gave nothing) */
    ZZAV_W_VPKTS,                               /* video packets taken */
    ZZAV_W_SHOW_US,                             /* the last second's average conversion into the overlay */
    ZZAV_W_NWORDS
};

/* ZZAV_CMD_PROBE's result: words at this offset (big-endian):
   [0] 0x50524F42 "PROB" when done, [1] n, [2] interval in us, [3..7] the formatter's MM2S
   CTRL, STS, PERIOD_CONFIG, BUFF_ADDR, BYTES_PER_CH, then n transfer counts */
#define ZZAV_PROBE_OFF      0x07AFC000UL
#define ZZAV_PROBE_N        256

/*
 * A packet in the ring: four words, then the data, padded to 4 bytes:
 *   [0] type << 24 | flags   [1] size   [2] pts high   [3] pts low
 * pts in the stream's time base, ZZAV_NOPTS if unknown.  A record never wraps:
 * ZZAV_PKT_WRAP says the rest of the ring is unused, go on at 0.  The 68k
 * writes data and header first, then ZZAV_W_WR; it keeps one byte free.
 */
enum { ZZAV_PKT_VIDEO = 1, ZZAV_PKT_AUDIO = 2, ZZAV_PKT_WRAP = 3 };
#define ZZAV_PKT_KEY        1
#define ZZAV_PKT_HDR        16
#define ZZAV_NOPTS          0x8000000000000000ULL

#endif
