# H.264 on the Z3660's ARM (FFmpeg's libavcodec): plan

Follows ARM-OFFLOAD-PLAN.md step 3, now that the firmware's MPEG-1 path is
measured from AMIX (vplay ARM mode: picture in the overlay, sound on the
board's HDMI, the 68060 nearly idle).  Written 2026-10-29 from the firmware
source (shanshe/Z3660 master 97d7dda); nothing here is measured on the
board yet.  Nothing is built or flashed before the user says so.

## What the firmware side offers

| What | Found |
|---|---|
| Board | Z-turn MYS-7Z020: Zynq-7020, two Cortex-A9 with NEON, 1 GB DDR |
| ARM clock | 667 MHz default; set_cpu_freq.c offers 767, 867, 900 |
| Runtime | bare metal, Xilinx standalone BSP, newlib (`-lxil -lc -lm`), no OS, no threads |
| Compiler flags | `-mcpu=cortex-a9 -marm -mfpu=neon -mfloat-abi=hard -O3` |
| Build without Vitis | `vitis_ide/Makefile` + `Z3660/Makefile`: arm-none-eabi-gcc, the BSP libraries are in the repo, BOOT.BIN by the open-source zynq-mkbootimage; prebuilt FSBL and bitstream in `design_1_wrapper/` |
| Core 0 | everything: RTG, soft3D, 68k register handler, audio, Ethernet, SCSI, USB, the MPEG-1 decoder (inside the register handler) |
| Core 1 | only the 68040 emulator (`Z3660_emu`, its own ELF at 0x30000000, started by writing 0xFFFFFFF0 + SEV); idle with a real 060 |
| Core 0's DDR | firmware 0x01000000-0x08000000 (48 MB heap, 4 MB stack); 060 RAM 0x08000000-0x18000000; board memory (RTG_BASE) 0x18000000-0x20000000 |
| Free DDR | 0x30000000-0x40000000, core 1's (the emulator's) region; codec37/47 (SMUSH) put delta buffers at 0x30000000 too. 0x20000000-0x30000000 is the Z3660's Zorro III RAM (the emulator maps it as "Z3 RAM"), not free |
| Recovery | BOOT.bin on the Z-turn's SD card, FAILSAFE.bin as fallback (mobotest menu copies between them); worst case the SD card is rewritten on the PC |
| Licence | firmware GPL v3; FFmpeg 4.4 LGPL 2.1 or later: may be linked in under GPL v3 (no --enable-gpl/nonfree parts needed for the decoders) |

## Design

**The decoder runs alone on core 1**, as its own ELF like Z3660_emu: FFmpeg
(libavcodec h264 + parser + libavutil, ~1-1.5 MB code) linked with newlib
into an image at an unclaimed DDR address.  Core 0 changes only by a few
lines: start core 1 when there is no emulator (emu==0), and reserve the
memory.  Why core 1:
- the MPEG-1 decoder runs inside core 0's register handler; while it decodes,
  every 68k access to board memory waits (measured: polling stalls);
- the RTG, soft3D, audio and SCSI keep their core;
- FFmpeg stays out of core 0's code and its memory map.

**68k <-> ARM interface: rings in board memory, no registers.**
- Packet ring (68k -> ARM): header {magic, version, write index, read index,
  flags}, then records {pts (int64, stream time base), size, flags (key,
  flush, eos)} followed by the data.  The 68k writes with move16, advances
  the write index; core 1 polls the index (no doorbell register, no ioctl,
  no core 0 involvement).
- Control block: commands (open codec with extradata = the avcC box, flush,
  close), the presentation clock from the 68k (media time + the 68k's wall
  clock, or "free run" on the ARM's own timer), the output rectangle and
  scale, statistics (frames decoded, dropped, decode time per frame).
- Output: core 1 converts YUV 4:2:0 -> BGRA with NEON straight into the
  overlay buffer, **scaling while converting** (the overlay itself cannot
  scale), double-buffered once the firmware patch is in.  Frames are shown
  at their pts against the clock; late ones are dropped on the ARM.
- Sound: phase 1 leaves it on the 68060 (Paula), but AAC 44.1 kHz stereo
  costs ~77 % of the 060.  Phase 2 decodes AAC on core 1 too and hands PCM
  to core 0's HDMI audio path (the way pl_mpeg's MP2 goes today), which also
  gives the ARM the master clock.

**vplay side**: a backend next to zzmpeg.c ("zzav.c"): libavformat demuxes
MP4/MKV on the 060 (cheap), sends the video packets into the ring instead of
decoding them, keeps the window, keys and overlay placement as ARM mode does
now.  Later the same backend as an FFmpeg decoder wrapper for ffplay.

**Cache coherency**: the 68k writes DDR through the FPGA, not through the
ARM's caches.  Core 1 maps the ring uncached (or invalidates each packet's
range before reading); overlay writes are flushed to L2/DDR per frame (the
same flush pl_mpeg does, with the corrected range).

## Build

1. Toolchain: arm-none-eabi-gcc (ARM GNU Toolchain 13/14, with newlib) --
   not on the PC yet; a download (~150 MB) and the user's OK first.  Plus
   zynq-mkbootimage (small, open source) for BOOT.BIN.
2. FFmpeg 4.4.8 (the tarball already used for AMIX), configured for bare metal:
   `--target-os=none --arch=arm --cpu=cortex-a9 --enable-neon
   --enable-cross-compile --cross-prefix=arm-none-eabi- --disable-everything
   --enable-decoder=h264 --enable-parser=h264 (later aac, mpeg4)
   --disable-pthreads --disable-os2threads --disable-w32threads
   --disable-network --disable-programs --disable-doc --disable-avdevice
   --disable-avformat --disable-swresample --disable-swscale --disable-avfilter
   --disable-runtime-cpudetect`, `-mfloat-abi=hard -mfpu=neon`.
3. Core 1 ELF from a Z3660_emu-style project: its own lscript.ld, BSP for
   ps7_cortexa9_1 (in the repo), main loop = poll ring -> decode -> convert.
4. Core 0: the start-core-1 change, built with the existing Makefile.
5. BOOT.BIN = FSBL + bitstream + core 0 ELF + core 1 ELF (system.bif as is).

## Steps

1. **Toolchain + FFmpeg build test on the PC** (after the OK): libavcodec for
   bare-metal ARM links; size; a newlib stub list (gettimeofday, sysconf,
   posix_memalign...).
2. **Speed test on the board, nothing changed in the firmware's work**: a
   core 1 image that decodes a clip already in DDR (put there by the 68k
   through board memory) and reports fps into board memory.  Answers the
   main question -- what resolution the A9 decodes in real time -- before
   any interface work.  Needs one BOOT.bin on the SD card (FAILSAFE.bin kept).
3. Packet ring + vplay backend, picture in the overlay, sound on Paula.
4. AAC on core 1 + HDMI sound, ARM clock as master.
5. MPEG-4 ASP (DivX/Xvid AVIs) through the same path.

## Risks

- **Speed**: guess only, from published Cortex-A9 numbers: one core at
  667-867 MHz with NEON should decode 640x360 in real time, 720p30 perhaps
  at 10-20 fps (bitrate dependent), 1080p not.  Step 2 measures it.
- **DDR bandwidth**: the 060's RAM is the same DDR; a decoder moving tens of
  MB/s may slow the 060 and the RTG scan-out.
- **Memory**: core 1's region 0x30000000-0x40000000 is free with a real 060
  (h264bench sits at 0x38000000, clear of SMUSH's buffers at 0x30000000);
  ask shanshe -- SMUSH's use of it shows the map is informal.
- **Core 1 and core 0 sharing the L2 and the GIC**: core 1 must not touch
  interrupts core 0 owns; no IRQs on core 1 at first (pure polling).
- **Overlay**: 1:1 only and the 1080p fetch bug (docs/z3660-overlay-*.patch);
  scaling in the conversion avoids the first, the patch the second.
- **Upstream**: the work is in the firmware, which the user does not
  maintain; a separate core 1 ELF keeps the core 0 change small enough to
  offer to shanshe.
- **FFmpeg on bare metal**: no threads (FFmpeg's frame threads off, one
  core anyway), newlib stubs, alignment of malloc (newlib memalign works on
  ARM).
