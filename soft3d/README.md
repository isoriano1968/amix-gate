# soft3D on AMIX

The Z3660's firmware contains soft3D: Wazp3D's triangle rasterizer, running
on the board's ARM. It's what gives AmigaOS Warp3D on the Z3660. This
directory makes it usable from AMIX.

How it works:
- The 68060 transforms, lights, clips and culls.
- The ARM draws the triangles, with Z buffer, Gouraud shading, textures and
  blending, into off-screen VRAM.
- The X server copies each finished frame into its window.

## What it needs

- **The kernel driver** from this repository, built with `-DZZ9K_MAP_BOARD`
  (and `-DZZ9K_TARGET_Z3660`). That lets a program map the whole board and
  adds the `ZZ9KIOC_SOFT3D` and `ZZ9KIOC_BLITX` ioctls. At boot the driver
  says `zz9000: mmap reaches device offset ... (soft3D)`.
- **For drawing into X windows,** the `Xzz9000` server with
  `x11/patches/rt17-soft3d-swap.patch`, which provides the ZZ9K-SOFT3D
  extension.
- **Firmware:** developed with Z3660 firmware 1.03 beta 21.

## Contents

| | |
|---|---|
| `soft3dtest.c` | The first test: draws one Gouraud-shaded, Z-tested triangle at the top right of the screen, then times `n` triangles. It needs no X server. |
| `libs3d/` | `s3d.c` and `s3d.h`: a small library for any rasterizer front end. It batches window-space triangles, keeps soft3D's draw state, uploads textures, clears, and presents the back buffer through the X server. The Mesa 3.1 port for AMIX uses it; the TinyGL port carries its own copy of the same logic. |

Built with [gcc14-cross-amix](https://github.com/asokero/gcc14-cross-amix):

```sh
m68k-elf-gcc -O2 -o soft3dtest soft3dtest.c
m68k-elf-gcc -O2 -I<X11R6.3 include> -c libs3d/s3d.c
```

## Measured

On an A3000 with the Z3660 (68060 at 80 MHz):
- `soft3dtest` draws about 700 Gouraud triangles of 28,000 pixels per second,
  about 20 Mpixel/s. A 68060 drawing the same triangle in software manages
  about 3.4 Mpixel/s.
- TinyGL on soft3D:
  - lit cube: 24.8 → 43.2 fps;
  - textured cube: 20.7 → 50.6 fps;
  - LameCubeAMIGA42 intro: 14.1 → 30.4 fps.

## Things to know

- **One program at a time.** soft3D has one state on the board. libs3d
  keeps an owner record in board memory and refuses a second user while the
  first one is alive. It cleans up after a program that died (`kill -9`),
  when the next one starts.
- **Free what you allocate.** The firmware's 16 MB heap does not check for
  running out. A full heap crashes the ARM, which resets the Amiga. libs3d
  frees its textures and Z buffer at exit and on SIGINT, SIGTERM and SIGHUP.
- **Z buffer width.** A Z buffer must be as wide as the bitmap's row (its
  stride), not the window. The firmware clears `height × stride` entries,
  and a narrower buffer overruns the heap.
- **Every operation is a blocking call.** Changing the draw state costs
  about 1.2 ms in the firmware. So libs3d applies state lazily, with the
  first triangle that needs it, and sends triangles in batches.
