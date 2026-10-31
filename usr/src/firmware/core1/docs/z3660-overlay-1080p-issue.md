**Title:** Hardware overlay at 1920x1080: each buffer row is shown on two screen lines (picture twice as tall, shifted)

**Firmware:** v1.03 beta 21 (commit 50e98e0); the code below is unchanged in current master.
**Setup:** A3000 with Z3660, real 68060, RTG screen 1920x1080. Overlay driven through the MPEG registers
(REG_ZZ_MPEG_* INIT/PROCESS/CLOSE, REG_ZZ_OVERLAY_XSTART/YSTART/WIDTH/HEIGHT) from a player under
Amiga UNIX (AMIX). The same should happen from AmigaOS, since nothing here is OS-specific.

### What happens

At 1920x1080 the overlay shows buffer row **L / 2** on screen line **L**. Every row of the overlay buffer
appears on two screen lines. An MPEG-1 picture of W x H written at overlay position (x, y) therefore comes out
twice as tall and starts at the wrong row: the window shows rows y/2 … (y+H)/2 of the buffer, so its upper part
shows whatever the buffer held there, and the lower part shows the top of the picture, stretched.

### How it was measured

A test program with no MPEG data (the decoder never starts):

1. Fill the overlay buffer (RTG_BASE + 0x06000000, pitch = screen width x 4) with 0 everywhere. Then write a
   640x352 pattern at rows 116…467, columns 0…639: horizontal bands of 32 rows, each band a different colour,
   which is where the firmware would write a frame at overlay_y = 116.
2. Write XSTART = 464, YSTART = 116, the MPEG PARAMs, then CMD_INIT (empty FIFO), then WIDTH = 640, HEIGHT = 352.
3. Photograph the screen.

Result:
- the top third of the overlay rectangle (screen lines 116…231) is black, i.e. buffer rows 58…115, which are 0;
- below that come the first bands, each **64** screen lines tall instead of 32;
- only bands 0…3 are visible inside the 352-line rectangle.

That is exactly screen line L → buffer row L / 2. Writing YSTART = 0 after the setup made no difference here,
since YSTART only moves where the ARM writes.

### Probable cause (from the source; not measured on the bus)

- `video.c`, `video_system_init()`: `init_vdma_ovl(mode->hres, mode->vres, 1, 1, RTG_BASE+0x06000000)`.
  The overlay VDMA fetches full screen-width lines (1920 pixels) no matter how wide the overlay is.
- The overlay stream is 32 bits wide (`XPAR_AXIVDMA_1_M_AXIS_MM2S_TDATA_WIDTH 32`), one pixel per beat. In
  `video_formatter.v` the overlay line buffer is refilled once per output line (`ovl_need_line_fetch`). If the
  1920 beats of a line take longer than one 1080p line (about 14.8 µs), the fetch for the next line is missed
  and lands one line later. The VDMA then advances one row per two screen lines.
- At 1280x720 the lines are shorter and last longer, so it should fit. I have not tested that mode.

### Suggested fix

Fetch only what the overlay shows. When the overlay is set up (`enable_overlay()`, or wherever WIDTH/HEIGHT
are taken):

- horizontal size = `overlay_w * 4` bytes;
- stride = screen width x 4, unchanged, as `pl_mpeg_handler.c` writes rows with `ovl_pitch = vs.vmode_hsize * 4`;
- vertical size = screen height, unchanged, so buffer row k still meets screen line k;
- start address = RTG_BASE + 0x06000000, unchanged. The ARM writes columns 0…W-1 and the formatter reads
  `overlay_buffer[counter_x - overlay_x]`, so column 0 of a line is the overlay's first pixel.

A 640-wide overlay then costs 640 beats a line instead of 1920. The VDMA has to be stopped and
reconfigured when the width changes, at vsync as `start_vdma_ovl()` already is.

### Smaller things noticed on the way (not bugs as such)

- The ARM decodes each frame straight into the buffer the overlay is showing, so lines tear. Two buffers,
  flipped at vsync, would fix it.
- `show_fps` is hard-coded on, so the frame rate is drawn into every frame. A PARAM bit to turn it off would help.
- Pixels written by the 68k into the overlay buffer must be in the ARM's byte order (B, G, R, A in memory).
  All-zero pixels show as black, not transparent. A note in `zzregs.h` would save the next person some time.
- Each 16-bit write to a 32-bit register triggers `write_rtg_register()` with the full long: first with the new
  high half and the old low half, then with both new halves. For REG_ZZ_MPEG_CMD_OP the first write repeats the
  previous command. It's harmless for PROCESS, but INIT straight after a CLOSE runs CLOSE again first.

### Patch

A patch for the suggested fix is attached (`z3660-overlay-fetch-width.patch`, against master 97d7dda,
`video.c` only). It is untested: I can't build the firmware here. I'm happy to test a build on the A3000
at 1920x1080, with the same test pattern and an MPEG-1 clip.

A second patch on top of it, `z3660-overlay-double-buffer.patch`, double-buffers the MPEG overlay against the
tearing. It also fixes the L2 flush after each frame, which covered rows 0…H-1 instead of the rows the
frame is written to (overlay_y…overlay_y+H-1). It is also untested; its description lists what to check.
