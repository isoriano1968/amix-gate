# Video on the Z3660's ARM: plan

Goal: the 68060 reads the file and hands the compressed data to the Z3660;
the board's ARM decodes, converts and scales, and the picture appears
straight in the RTG framebuffer.  The 060 keeps demuxing, the clock, sound
through Paula, and the user interface.

Written 2026-10-29, after ffplay first ran on AMIX.  Everything under
"Known" was measured on the A3000 (68060 at 80 MHz, Z3660, firmware v1.03
beta 21); everything under "Open" still has to be read in the firmware
source or tried.

## Known

| What | Measured |
|---|---|
| MPEG-4 ASP 320x180 decode on the 060 | 55 ms a frame (18 fps) |
| H.264 main 320x180 decode on the 060 | 125 ms a frame (8 fps) |
| MP2 22 kHz mono decode | ~24 % of real time |
| AAC 48 kHz stereo decode | ~77 % of real time (after the FPU rounding fix) |
| YUV->RGB on the 060, 320x180 | ~20 ms (tables), 22 ms (swscale) |
| A system call | ~0.27 ms; gettimeofday 107 us; a soft3D op as ioctl 0.3-1.2 ms |
| soft3D SETDRAWSTATE | ~1.2 ms in the firmware |
| 060 stores to board memory | ~10 MB/s (longs), 26 MB/s (move16) |
| ffplay, sample2-amix.avi (35.8 s) | 45.7 s wall, 36 s user, 8 s system, 267 of 447 frames dropped |

Today every soft3D operation is one ioctl, and the 060 waits in it until
the ARM is done: the two processors never work at the same time.

## The interface (needed by every step below)

1. **A command ring in board memory.** The 060 writes commands (op code,
   parameters, pointers into board memory) into a ring and advances a write
   index; the ARM works through them and advances a read index and a
   completion counter.  One doorbell write per batch instead of one ioctl
   per operation, and the 060 goes on while the ARM works.
2. **The doorbell without a system call.** The kernel driver
   (zz9000.c, ZZ9K_MAP_BOARD) maps board memory but reaches the registers
   only through ioctls.  Either map the one doorbell register page to user
   space, or let the ARM poll the ring's write index (no register at all).
3. **Completion without a system call.** The 060 reads the completion
   counter from board memory; waiting costs a sched_yield / short sleep,
   not an ioctl.
4. **Ownership** as libs3d does today: an owner record in board memory,
   freed by the next user when the owner died.

## Steps

### 1. libs3d on the command ring (firmware + libs3d)
All of soft3D's operations go through the ring: state, triangle batches,
texture updates, flush, swap.  The firmware caches draw state so a repeated
SETDRAWSTATE is cheap.  Benefits everything that uses soft3D: the SDL zz9k
renderer (ffplay), Mesa, TinyGL, GLQuake, ioquake3 (~300 ioctls and 117 state
changes a frame today).

### 2. YUV 4:2:0 -> screen (firmware op + SDL zz9k + vplay)
New op: Y, U, V planes in board memory -> converted, scaled (nearest or
bilinear) into a rectangle of the framebuffer or the soft3D back buffer,
clipped to a list of rectangles (the window's visible parts).  The 060 only
copies the decoded planes (86 KB for 320x180 instead of 230 KB of RGBA) and
converts nothing.  In SDL: IYUV textures of the zz9k renderer become plane
uploads plus this op; ffplay needs no change.

### 3. Decoding on the ARM (firmware + FFmpeg)
libavcodec's H.264, MPEG-4 ASP, MPEG-1/2 video decoders built for the ARM
(bare metal in the firmware, NEON where FFmpeg has it), running from a frame
pool in board memory:
- the 060 sends packets (pts, data) into the ring, the ARM decodes into the
  pool, step 2's op puts a frame on the screen at its time;
- FFmpeg on AMIX gets a decoder wrapper ("h264_z3660", "mpeg4_z3660") that
  forwards packets and returns frames as handles to board memory, plus a
  pixel format for them that the zz9k renderer shows without copying;
- vplay first (our own player, no API constraints), ffplay after.
Audio (AAC, MP3) on the ARM too, returning PCM for Paula, if the ARM has the
time; MP2 can stay on the 060.

### 4. Transform and lighting on the ARM (Mesa, TinyGL, Quake)
Not video, but the same ring: vertices and matrices to the board, the ARM
transforms, clips and lights.  After step 1, when the ARM has room.

## Found in the firmware (shanshe/Z3660, ~/Sources/usr-amix/upstream/Z3660, 2026-10-29)

Board: Z-turn MYS-7Z020 (Zynq-7020, two Cortex-A9 with NEON); firmware in
z3660-firmware/Z-TURN/vitis_ide/Z3660/src (bare metal, Xilinx Vitis).

- **Core 1 is free with a real 060.** main.c starts it only for the 68040
  emulator (emu==1, a separate ELF from the Z3660_emu project, started with
  SEV after writing its entry to 0xFFFFFFF0).  Everything else -- RTG,
  soft3D, AHI/MHI (with the AX filters), Ethernet, SCSI emulation, USB --
  runs on core 0.  A decoder on core 1 would not compete with the RTG.
- **MPEG-1 decoding on the ARM already exists** (v1.03 beta 21 has it):
  mpg/z3660_mpeg_handler.c with pl_mpeg (MPEG-1 video).  Registers
  REG_ZZ_MPEG_CMD_OP 0x2C0, _STATUS 0x2C4, _PARAM0-4 0x2C8-0x2D8; commands
  INIT, PROCESS, RESET, GET_INFO, CLOSE, DISPLAY_FRAME; a 1 MB FIFO and a
  shared state block (arm_decoder_shared_arm_t: status, frames decoded,
  sync time, size, frame rate, decode time ...) in board memory, written by
  the 68k.  Frames are converted to BGRA on the ARM and shown in the
  hardware overlay.  It runs on core 0, inside the register handler.
- **A hardware overlay plane**: 32-bit pixels at RTG_BASE+0x06000000,
  position and size by register (MNTVF_OP_OVERLAY_XY/WH), its own VDMA,
  transparent where the pixel says so.  No free scaling: the VDMA takes only
  integer dividers (hdiv/vdiv).
- **DMA-style RTG operations** (rtg/dma_rtg.c, str_dmaop.h): parameter
  blocks in memory, one register write -- the pattern for the command ring.
- Also there: mp3/ (miniaudio, stb_vorbis), rtg/codec37/47 (SMUSH), NEON
  memcpy, soft3D with NEON intrinsics and tiling.

So the plan changes: **step 0 -- use the MPEG-1 decoder that is there.**
vplay (then FFmpeg as a decoder "mpeg1video_z3660") sends MPEG-1 streams
through the FIFO and the 060 does nothing for the picture but feed it;
mkamixvideo.sh can make MPEG-1 + MP2 files for it.  That gives a measured
ARM decoding path before any firmware is changed.  Later steps move the
decoder to core 1 and add FFmpeg's decoders (H.264, MPEG-4) next to pl_mpeg.

## Still open

- The firmware's memory map: free RAM for a frame pool and FFmpeg's working
  memory (soft3D's heap is a fixed 16 MB at board 0x4300000).
- How the firmware is built (toolchain, libc), so libavcodec can be linked
  in; licence (FFmpeg LGPL 2.1) for distributing the firmware.
- Cache coherency between the ARM and memory the 060 writes over Zorro III.
- How updates are flashed, and how to get back from a bad build.

## Order and first measurement

1. Step 0: the firmware's MPEG-1 path from AMIX (no firmware change).
2. Step 2 alone, with ioctls as they are: YUV op, SDL zz9k and vplay.
   Measurement: ffplay frames dropped, 060 time per frame.
3. The ring (step 1), then step 2 on it.
4. Step 3 for MPEG-4 ASP (simplest), then H.264.

Expectations: step 2 takes ~20 ms of conversion off each frame; step 3 takes
the 060 out of video decoding altogether, so H.264 at 320x180 and larger
becomes a question of the ARM's speed, not the 060's.
