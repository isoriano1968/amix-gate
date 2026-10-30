# Core 1 as a GPU: a Gallium driver for the Z3660

Goal: GL on AMIX works like on a machine with a graphics card.  The 060
runs Mesa's device-independent part (the GL API, state tracking, the GLSL
compiler); a small hardware driver hands the work to the "GPU", which today
is the Z3660's second ARM core.  Later Mesa versions (shaders) and a real
graphics card only replace a library, not the programs.

## Why soft3D is not enough

soft3D (Wazp3D in core 0's firmware) only rasterises: the 060 still runs
Mesa 3.1's whole pipeline, and every soft3D operation is a synchronous ioctl
that blocks while core 0 draws, so the 060 and the ARM never overlap.
ioquake3 at q3dm1's spawn point, ms per frame (2026-10-27): game + front end
75, state setup 150 (117 SETDRAWSTATE at ~1.2 ms), transform/clip 40,
batching 40, ARM drawing with the 060 waiting 125 = ~433-483 (2.1 fps).

## The layers

```
 060 (AMIX)                                         Z3660 core 1
 +------------------------------+
 | program (GL 1.x ... 3.3)     |
 | libGL: Mesa (newer version)  |
 |   GL API, st/mesa, GLSL->TGSI|
 |   Gallium interface (pipe_*) |
 | zz3660 Gallium driver -------+--- command ring --> zz3660 executor
 |   encodes pipe_context calls |    (board memory)    softpipe-based:
 |   buffers/textures in board  |                      TGSI interpreter,
 |   memory                     | <-- fences --------  rasteriser, NEON
 | xlib target: GLX on plain X  |                      -> back buffer in VRAM
 |   swap via ZZ9K-SOFT3D       |
 +------------------------------+
```

- **The boundary is Gallium's pipe interface**, the same one Mesa's real
  hardware drivers implement.  The zz3660 driver is a thin encoder, like
  Mesa's virgl driver (which sends Gallium commands to a virtual GPU): state
  objects, buffers, shaders (TGSI), draw calls and fences go into the ring.
- **Core 1 is the GPU.**  Its program (`z3660ARM_Core_gpu`) executes the
  stream with Mesa's own softpipe code (same Mesa version, bare metal,
  newlib): vertex shaders, clipping, rasteriser, fragment shaders.  Hot paths
  get NEON later; a TGSI-to-ARM compiler is a possible later step.
- **GLX without DRI:** Mesa's gallium "xlib" target (libgl-xlib) gives GLX on
  a plain X server, as used by softpipe/llvmpipe.  Its display step goes
  through the existing Xzz9000 ZZ9K-SOFT3D Swap extension (window clipping,
  overlapping windows intact).
- **Pipelining:** SwapBuffers of frame N queues the frame and waits only for
  frame N-1, so the 060 builds the next frame while core 1 draws.
- **Buffers and textures** live in board memory, written once by the 060
  (move16), read by core 1 directly; no copy through the ring.
- **A real card later** = another Gallium driver library in place of zz3660
  (plus whatever kernel support that card needs on AMIX).  Programs, GLX and
  Mesa stay.

## Decision 2026-10-30: GL marshalling, Mesa on core 1

Phase 0 and the fast paths settled the boundary.  The 060's share of Mesa
18.3 (noop driver) is 108 ms per frame of the synthetic q3 scene before any
game code, while core 1 runs the *whole* Mesa pipeline for that scene in
~93 ms (front end ~2.4 ms of it).  So the 060 does not run Mesa: its libGL
("zzgl") packs the GL calls and their data into a ring in board memory, and
core 1 (`z3660ARM_Core_gpu`) unpacks them into its own Mesa 18.3 (OSMesa,
softpipe with the fast paths) and shows the frames in the RTG overlay at the
window's place.  This is the fallback named below; the "a real card later is
just another library" property holds the same way.

- **Encoder and decoder are generated** from Mesa's gl_API.xml (names,
  parameter types, array sizes); calls with client arrays, pixel data and
  replies (glGet*, glGenTextures, ...) are written by hand.
- **Byte order:** words big-endian in the ring (the 060 writes plainly);
  core 1 swaps.  Pixel bytes (GL_UNSIGNED_BYTE) are not swapped.
- **Client arrays** (glVertexPointer ... glDrawElements, CVA locks) go into
  the ring with the draw that uses them (the range of indices used).
- **Replies:** a call that returns something flushes the ring and waits.
- **SwapBuffers** queues the frame; the 060 waits only when the ring is full,
  so it builds the next frame while core 1 draws this one.
- **Sound:** the game mixes into the board's HDMI audio ring (the one vplay's
  player uses); core 1 publishes the play position (0x7FC00128) in its
  control block for the 060.  Paula stays free.

## Mesa version

The newest Mesa that still fits AMIX without a big runtime port.  Candidate:
**Mesa 18.3** (GL 3.3 core / 3.0 compat with softpipe, GLSL 3.30, TGSI,
the xlib target, virgl as a model; the last release with the autotools build,
and before NIR replaced TGSI in most drivers).  Needs on AMIX: C99 (gcc 14:
fine), pthreads (gnu-pth-amix, or Mesa's thread code reduced to a stub: it
runs single-threaded), Python + Mako on the PC only (build time).  The
library is several MB: a shared libGL becomes worth it (see the shared library
plan; gcc 14 m68k -fPIC has the GOT bug, so first non-PIC TEXTREL shared
objects as for ioquake3's modules).

## Two things that can make it slow

- **The 060's share.**  A newer Mesa does more per draw call than Mesa 3.1
  (state validation, vbo module).  For old fixed-function games (GLQuake,
  ioquake3's GL1 renderer) this could eat the gain.  Mesa's gallium **noop**
  driver runs everything except drawing: it measures the 060 side exactly.
- **060 to board bandwidth:** 10.6 MB/s with long stores, 26 MB/s with
  move16.  ioquake3 sends ~25k vertices (~0.8 MB) per frame: 30-80 ms.
  Vertex buffers that stay in board memory (static geometry, ioquake3's GL2
  renderer, VBOs) avoid sending them again.

If the 060 share is too big, the fallback is to move the boundary up: GL
calls are marshalled (like Mesa's glthread) and Mesa runs whole on core 1.
Same executor, same core program; only the AMIX library differs.

## Expected result (estimates; phase 0 checks them)

| program | now (soft3D) | core 1 GPU |
|---|---|---|
| Mesa gears | 18.5 fps | 25-45 |
| GLQuake 320x240 | 3-6 fps | 8-15 |
| ioquake3 q3dm1 | 2.1-3 fps | 4-8 |
| GLSL programs | impossible | possible (slow at first) |

## Phases

0. **Measure (go/no-go).**
   a. Mesa 18.3 built for AMIX with the noop driver: gears, GLQuake and
      ioquake3 time per frame on the 060 alone.
   b. softpipe built bare metal for the A9: gears and a textured scene timed
      in QEMU (correctness) and on core 1 via Reload (speed).
   c. 060 to board write speed for a vertex stream (move16).
1. **Command stream + executor.**  Encoding of pipe_context (modelled on
   virgl's protocol), ring and fences as in zzav, `z3660ARM_Core_gpu`.
   A PC harness (like zzav_host.c) runs the stream into softpipe on the PC
   and compares pictures.
2. **zz3660 Gallium driver + xlib target for AMIX**, fallback to softpipe on
   the 060 if core 1 does not run the GPU core.
3. **Bring-up:** gears, the Mesa demos, the intros, GLQuake, ioquake3 (GL1,
   then GL2 renderer with shaders).
4. **Speed:** NEON in softpipe's hot paths, buffers kept in board memory,
   pipelining depth, maybe TGSI-to-ARM code.
5. **Later:** one core program for video + GPU (each core reports an identity
   word that libGL, vplay and ztopcde read), ztopcde shows the GPU's load,
   shared libGL.

## Rules carried over

Core 1 never does whole-cache (L2) maintenance; shared areas are mapped
uncached on core 1; core 0's firmware is not changed; the ARM stays at
666 MHz; core 1's region 0x30000000-0x40000000 is free with a real 060.
