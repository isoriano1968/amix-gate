# core1-z3660-amix

The Z3660's second ARM core as a coprocessor for Commodore Amiga UNIX (AMIX):
while the 68060 runs the system, core 1 of the Zynq (Cortex-A9 with NEON)
decodes video and renders OpenGL. Developed on an A3000 with a real 68060.

Programs for core 1 are named `z3660ARM_Core_<name>.bin`. AMIX loads them over
the running firmware, without a new boot image.

## Cores

| Core | What it does |
|---|---|
| `cores/h264bench` | H.264 and AAC player: picture in the RTG overlay, sound on HDMI. Also holds the harness all cores share (`arm_main.c`, `lscript.ld`) |
| `cores/gpu` | OpenGL server: Mesa 18.3 (softpipe with NEON fast paths) fed by GL calls from the 060, frames in the overlay, sound on HDMI |
| `cores/glbench` | Timed GL scenes, with PMU profiling |

Measured with the ARM at 867 MHz: H.264 640x360 decoded in about 9 ms a frame;
ioquake3 at 640x480 at about 6-7 fps.

## The rest

- `tools/armbench.c`: the AMIX loader: `-load`, `-reload`, `-dump`.
- `tools/ztop.c`: the board's and core 1's state as text. The CDE window is
  `ztopcde` in [cde-apps-amix](https://github.com/isoriano1968/amix-userland/tree/main/components/desktop/cde-apps).
- `tools/ovloff.c`: frees a player left running and switches the overlay off.
- `tools/glshow.c`, `tools/zzglstat.c`: GL demo on the overlay, statistics.
- `zzgl/`: the GL protocol: 68k encoder, core 1 decoder. `bench/`: the scenes.
- `patches/`: our changes to Mesa 18.3.6. `build/xbuild.py`: its cross build.
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

The players on the AMIX side use `cores/h264bench/zzav.h`.

## On the box

    armbench -load z3660ARM_Core_h264bench.bin     # the first after power-on
    armbench -reload z3660ARM_Core_gpu.bin         # afterwards
