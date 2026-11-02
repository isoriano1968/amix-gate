# firmware/core1

The Z3660's second ARM core as a coprocessor for Commodore Amiga UNIX (AMIX):
while the 68060 runs the system, core 1 of the Zynq (Cortex-A9 with NEON)
decodes video and renders OpenGL. Developed on an A3000 with a real 68060.

There are two ways to the GL on core 1, and both stay:

- **zzgl:** the fast path. GL calls go straight to Mesa on core 1, for games and
  intros. `libzzgl.so.1` is what a program links.
- **zz3660:** the compatible path. Mesa 18.3 runs on the 060 as the system's
  `libGL.so.18`, with GL 3.1 and GLSL 1.40, and its Gallium driver sends the work to
  softpipe on core 1. A driver for a future GPU only has to replace this one.

The packages are `x11/mesa-183` in
[amix-userland](https://github.com/isoriano1968/amix-userland), which also has the
games and demos; this directory has what they are built from.

Programs for core 1 are named `z3660ARM_Core_<name>.bin`. AMIX loads them over
the running firmware, without a new boot image.

## Cores

| Core | What it does |
|---|---|
| `cores/h264bench` | H.264 and AAC player: picture in the RTG overlay, sound on HDMI. Also holds the harness all cores share (`arm_main.c`, `lscript.ld`) |
| `cores/gpu` | OpenGL server: Mesa 18.3 (softpipe with NEON fast paths), fed by zzgl's GL calls or zz3660's Gallium stream from the 060, frames in the overlay, sound on HDMI |
| `cores/glbench` | Timed GL scenes, with PMU profiling |
| `cores/amix` | gpu and h264bench in one image (`z3660ARM_Core_amix.bin`, 8.9 MB): GL server, H.264 player and decoder; a posted job is GL when it carries `ZZGL_JOB_MAGIC`. zzgl, libGL and the launchers take it for gpu |

The heap: gpu and amix (`-DHB_HEAP_LOW`) start it below the image, in core 1's
DDR from `0x30000000`, and go on above the image: about 245 MiB for gpu,
242 MiB for amix, against 104 MiB before (`arm_main.c` `_sbrk`). Core images
may be up to 9 MiB (`HB_IMAGE_MAX`): the reload window plus the megabyte of
core 0's MPEG sound after it, unused while core 1 is idle. A core still
running an older harness takes images of 8 MiB only: reload gpu first, then
amix.

Measured with the ARM at 866 MHz: H.264 640x360 decoded in about 9 ms a frame;
timedemo demo1 of GLQuake at 640x480 7.9 fps and at 320x240 15.4 fps on zzgl, 2.4 fps
at 320x240 on zz3660.

## The rest

- `tools/armbench.c`: the AMIX loader: `-load`, `-reload`, `-reload -force` (a hung gpu core, through its timer; unreliable, see below), `-dump`.
- `tools/ztop.c`: the board's and core 1's state as text. The CDE window is
  `ztopcde` in [desktop/cde-apps](https://github.com/isoriano1968/amix-userland/tree/main/components/desktop/cde-apps).
- `tools/ovloff.c`: frees a player left running and switches the overlay off.
- `tools/glshow.c`, `tools/zzglstat.c`: GL demo on the overlay, statistics.
- `zzgl/`: the GL protocol: 68k encoder, core 1 decoder, `build-shared.sh` for
  `libzzgl.so.1`. `bench/`: the scenes.
- `src/mesa-18.3.6/src/gallium/drivers/zz3660/`: the Gallium driver: encoder for the
  060, executor and transport for core 1.
- `patches/`: our changes to Mesa 18.3.6. `build/xbuild.py`: its cross builds,
  `build/libgl-amix.sh`: links `libGL.so.18`.
- `docs/`: plans, measurements, the overlay's limits at 1080p.

## Firmware

The loader needs core 0's side: the `core1-loader` branch of
[isoriano1968/Z3660](https://github.com/isoriano1968/Z3660) (loader block,
overlay fetch width, double buffering).

## Building

Needs `arm-none-eabi-gcc`, for the tools gcc for AMIX, and a checkout of that
firmware for its BSP, by default in `../Z3660` (`make Z3660=<dir>` otherwise).

    # H.264: FFmpeg 4.4.8 unpacked as ffmpeg-4.4.8/
    sh configure-arm.sh && make -C build-arm
    make -C cores/h264bench

    # GL: Mesa 18.3.6 unpacked as src/mesa-18.3.6, patched with patch -p1,
    # its meson build for the PC in src/host-osmesa
    python3 -I build/xbuild.py arm
    make -C cores/gpu
    make -C cores/glbench
    make -C cores/amix

    # libGL.so.18 for AMIX; STAGE has the shared X libraries
    python3 -I build/xbuild.py amix-glx
    sh build/libgl-amix.sh STAGE
    sh zzgl/build-shared.sh STAGE

The players on the AMIX side use `cores/h264bench/zzav.h`.

## On the box

    armbench -load z3660ARM_Core_h264bench.bin     # the first after power-on
    armbench -reload z3660ARM_Core_gpu.bin         # afterwards

`-reload -force` is not dependable yet: of two tries during drawing, one left the next job faulting in `free` and one left core 1 dead until a power cycle.

