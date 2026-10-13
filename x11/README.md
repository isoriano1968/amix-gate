# x11/ — the X11R6.3 `Xzz9000` server on the Z3660 RTG (cross lane)

- `startx-rtg` — start the server with the mode chosen first, then the user's session
  (`~/.xinitrc`, i.e. the X11R5 `olwm` etc., unchanged); see the script header.
- `patches/zz9000hw-modes.patch` — the one source deviation from isoriano's tree
  (two mode entries), applied to the build tree only and proposed upstream.
- `patches/p5-*.patch` — P-5 correctness fixes to the DDX, applied in name order after the
  mode patch (build tree only, each its own commit):
  - `p5-1-solidrect-alu.patch` — S1, PolyFillRect honours the raster op.
  - `p5-2-copyarea-expose.patch` — S2, window-to-window CopyArea copies only the visible
    source and returns the GraphicsExpose region.
  - `p5-3-setspans-alu.patch` — S4, SetSpans honours the raster op (PutImage, mi CopyArea).
  - `p5-4-copyarea-alu.patch` — S5, non-GXcopy window-to-window CopyArea goes to miCopyArea.
  - `p5-5-copyarea-onebox.patch` — fix 2a, the one-box fast path for unobscured window copies.
  - `p5-6-stores32.patch` — fix 2, 32-bit stores in the fill/copy/span loops.
  - `p5-7-shortruns.patch` — fix 2 follow-up: runs under 4 pixels keep the 16-bit loop.
- `patches/rt04-monsw-hook.patch` — RT-04 (P-8), applied after the p5 series: the rtg/ layer
  asks the card's own device for the monitor switch (SVGAIOCSetMonitorSwitch, Burckert's
  numbering) at screen init and at exit, and keeps a dup() of the card fd so a server reset does
  not flip the monitor.
- `patches/rt07-card-blitter.patch`, `rt07b-small-fills.patch`, `rt07c-zeroline-bias.patch`,
  `rt08-banded-default.patch` — P-5 fix 3: window fills and copies go to the card's blitter
  through the driver's `ZZ9KIOC_BLITS`.
  - `-blitmin` (default 1024) and `-blitmax` (default 8192) are built in, so startx-rtg needs no
    flags.
  - Every card operation is split into bands of at most 8192 px, so the 68060 keeps taking
    interrupts: MG10 lost 42–44 % of serial input without banding.
  - The zero-width line bias is set, so diagonal edges draw the same in every build.
  - On a kernel without the blitter, the server logs one refusal and draws on the CPU.
- `patches/rt10-console-keyboard.patch` — RT-10: when the server ends, it hands the screen
  manager's keyboard selection back to the console (SIOCBACK). Before this, typing on the console
  went nowhere after every RTG X session; a `kill -9` of the server still loses it.
- `rtg-kbdback.c` — RT-11: gives the console its keyboard back after a server that died without
  cleaning up (`kill -9`). It opens its own screen, selects it (`SIOCACTIVATE`), sends it to the
  back (`SIOCBACK`), and closes it. startx-rtg runs it from its exit trap, and only when the server
  died on its own; this shell's `wait` returns 0 whatever the child's status, so the status can't
  tell. Cross-built:
  `m68k-cbm-sysv4-gcc -O -I<usr-amiga>/usr/amiga/include -o rtg-kbdback x11/rtg-kbdback.c -lscreen`
  (with `AMIX_SYSROOT` as for Xzz9000). It needs `/usr/amiga/lib/libscreen.so`, like the server.
  Install it as `/opt/amix/bin/rtg-kbdback`, next to `startx-rtg`.
- `KIT.sha256` — the X11KIT=rtg set of record: the server, rtg-kbdback, startx-rtg and
  xinitrc-rtg, as installed on the box (binaries and copies in `build/out/`).
- `SERVERS.sha256` — the server builds of record: s8rt04 (K1 until replaced) and s14rt04 (the
  release candidate: s13rt04's banded blitter, MG11-measured, plus RT-10). The binaries live in
  `build/out/` (gitignored); the patch chain from cb61a21 rebuilds them.

**Monitor switch (P-8 / RT-04).** On a kernel with `/dev/monsw` (`monswctl status` succeeds), the
server switches the monitor itself through the card driver and the kernel's monsw, and
`startx-rtg` does nothing about it. On a kernel without monsw (the r13 lineage), `startx-rtg`
falls back to `zz9k_monsw`, driving the line itself from `/etc/monsw` (`RTG_MONSWITCH` is a test
override). The ship rule: an image carries the hooked server together with a monsw kernel. The
retired interim pieces are RT-03's line driving in the driver and `/etc/rtg-monswitch`. See
`docs/monsw.md` in amix-kerntools (https://github.com/jusii/amix-kerntools).

The build recipe, the established facts and the deviations are in `docs/X-CROSS.md`;
the build itself lives under `2026-09-18-xzz9000-cross/` (kept outside the repo).
The recipe's host-side inputs, `host.def` and `imakecpp.sh`, are tracked in `x11/cross/`
(scrubbed copies; `x11/cross/README.md` lists the substitutions and the copies of record).

Status (2026-09-26): metal-proven. `startx-rtg` brought the OpenLook desktop up on the Z3660
HDMI (`docs/DEMO-RTG.md` row 7). Performance is the next lane.
