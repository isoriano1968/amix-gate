# Changelog

## 1.0 (amix-z3660rtg) - 2026-10-13

Initial public release of the fork: the Z3660 accelerator's onboard RTG as a second target.
The default build is upstream's ZZ9000 driver; `/dev/zz9000`, major 49 and the ioctl ABI
are unchanged.

### Driver
- Board table: identity, window, register file and mode table are data. The default rows
  reproduce upstream's behaviour. The Z3660 row is behind `-DZZ9K_TARGET_Z3660`: a 32-bit
  register file from 0x100, FW_VERSION at 0x1A0, the framebuffer as the whole device
  space, and modes 1920x1080 and 1280x800.
- Bench rows `-DZZ9K_BENCH_Z3` and `-DZZ9K_BENCH_Z3660` for amiberry's ZZ9000 Zorro III model
  and the Z3660-RTG stand-in.
- `ZZ9KIOC_CONPUTS` and the framebuffer console on the Z3660.
- The RTG monitor switch through the kernel's monsw (`-DZZ9K_MONSW`) and `ZZ9KIOC_GETMONSW`.
- The card blitter through `ZZ9KIOC_BLITS`, with a register cache. An optional non-cached
  framebuffer class (`-DZZ9K_FB_NC`) was measured and gave no gain.
- 68060 safety: no constant division (it compiles to 64-bit `MULS.L`), one message per miss.

### Tools
- `zz9k_cat`, `zz9k_reg`, `zz9k_demo`, `zz9k_maptest`, `zz9k_memprobe`, `zz9k_mode`,
  `zz9k_monsw`, `zz9k_rw`.
- `zz9k_test` accepts 1920x1080 and 1280x800, raises its own VM limit, and restores the display
  on an mmap failure.
- `bench/zzpanfix.c`, the stock-amiberry pan workaround.

### X (x11/)
- Build-tree patches for isoriano's X11R6.3 `Xzz9000`:
  - the two Z3660 modes;
  - the P-5 correctness fixes: raster ops in PolyFillRect, SetSpans and CopyArea, and
    window-to-window CopyArea exposures;
  - 32-bit stores;
  - the monitor-switch hook;
  - the card blitter in 8192-pixel bands;
  - the zero-width line bias;
  - the console keyboard handed back on exit.
- `startx-rtg`, `xinitrc-rtg` (the session ends when olwsm exits; no screen saver) and
  `rtg-kbdback`. The kit of record is pinned in `x11/KIT.sha256`.
- `x11/cross/`: the cross-build inputs (`host.def`, `imakecpp.sh`).
- `x11/bench/`: the measurement kit. It has x11perf R6.3, xbench, the discriminator clients and
  `zz9k_blitbench`, with a reproducible build.

### Validated
- Bench steps 0–3 on amiberry's ZZ9000 emulation and the Z3660-RTG stand-in.
- On metal (A4000, Z3660, 68LC060): the OpenLook desktop on the Z3660 HDMI, the release kit's
  correctness gates, and the blitter speed-ups.
- User `mmap` of `/dev/mem` stays broken on the r13 kernel lineage (the 68040/68060 port's
  ISSUE-46). It does not affect `/dev/zz9000`.

## 0.1.0 - 2026-07-03

- Initial public AMIX release.
- Zorro II product 3 discovery and attachment.
- Mode setting and RGB565/32-bit framebuffer mapping.
- Framebuffer information, fill, copy, pan, and display-switch ioctls.
- Color framebuffer console with late SysV init activation.
- Diagnostic, mode-test, console-control, and colorls tools.
- Interface used by the ZZ9000 X11R5 and X11R6.3 RTG servers.
