# BENCH-STEP2-STANDIN.md — contract for the amiberry Z3660-RTG stand-in

> **Out-of-repo citations.** Evidence directories are cited by their date-stamped names. They, sibling repositories and archived images are kept outside this repo and are not needed to build or use the driver.

**Result: PASS, 2026-09-16** (kernel 40217, driver bytes c5a5a46, amiberry stand-in 814d25c +
pan fix 71e7527; evidence `2026-09-16-zz9000-rig-step2/`, report `31-report.md`;
verified here: transcript lines exact, access log free of anything below `0x100`
or in `0x1000-0x1FFF`, register page readable through `read(2)` with `0103` at `0x1a2`). Two
rig facts: `chipset=aga` runs with **`chipset_compatible=A3000`** (the A4000 profile breaks the
2.04/WD Amix boot); the bars render **without** `zzpanfix` — the amiberry X2==0 fix works, the
driver's X2=0/PAN=0 sequence gives the native stride as on the firmware. Ruling on the one
note: `read(2)` at or past `board_size` returns **ENXIO**, not a 0-byte EOF — that is
upstream's `zz9000rw` bound (fixed at attach, not mode-dependent), the same on every row and
in steps 0/1 (masked there by `2>/dev/null`); kept as the intended fail-closed semantics of a
device window, criterion 5 reworded.

**For amiberry (the model) and the amix-kerntools build rig (the run).** Bench step 2
of DESIGN.md §6 rehearses the driver's Z3660 row end to end on the piscsi rig before metal.
It needs an amiberry device that behaves like the Z3660 firmware's RTG *as the guest sees it*
(DESIGN.md §3, `docs/FIRMWARE-RTG-INTERFACE.md`), not like the MNT ZZ9000: the register
file is a different ABI, so remapping `zz9000_bank` to another address is **not** enough.
Everything below is a firmware fact with a source citation unless marked *choice*.

## 1. Board placement

| Item | Value | Source |
|---|---|---|
| Guest base | `0x10000000`, size `0x08000000` (128 MB) | `cpu_emulator.cpp:221-226`, `init_rtg_bank` `uae_emulator.cpp:1019-1023` |
| AutoConfig | **none** — the board never appears in the guest's AutoConfig walk; `bootinfo.autocon[]` must not list it | same; the driver's fixed row relies on this |
| How to map | `map_banks()` at `0x10000000 >> 16` for `0x800` banks from an init path outside `gfxboard_init_board()`/autoconfig — precedent `debugmem_bank` (`src/memory.cpp:1536,2755-2764`) | *choice* |
| Enable key | `gfxcard_type=Z3660_RTG` + `gfxcard_size=128` (rides amiberry's RTG display/toggle path); off unless set; never together with a `gfxcard_type=ZZ9000_*` line | amiberry 814d25c, `docs/z3660-rtg.md` |
| Rig chipset | the driver's fixed row is AGA-gated: `((VPOSR >> 8) & 0x7f) >= 0x22`. amiberry returns `0x23` for AGA, `0x20` for ECS PAL, `0x30` for ECS NTSC (`src/custom.cpp:2413-2425`); `cs_agnusrev` is not settable from a `.uae`. So the step-2 rig runs **`chipset=aga`** (the metal's chipset) — or, as a bench shortcut, `ntsc=true` with ECS, which the shared gate also accepts | |
| Coexistence | the rig's piscsi model stays at `0x40000000` as `0x144B0001`; the driver build for step 2 is `make=ZZ9K_TARGET=-DZZ9K_TARGET_Z3660,ZZ9K_BENCH=-DZZ9K_BENCH_Z3660` (the second define drops the row that would otherwise read that AutoConfig entry as "autoconfig_rtg YES" and stop) | DESIGN.md §4.2 |

## 2. Window layout (offsets from the base)

| Range | Behaviour | Source |
|---|---|---|
| `0x000000-0x000FFF` | the register file (§3); unmapped offsets read `0xFFFFFFFF`, writes ignored | `rtg.c:1023-1026,2417-2420` |
| `0x001000-0x001FFF` | raw RAM (the firmware's test area; it logs every access on its UART — the model may log once, must not spam) | `rtg.c:573-577,646-673` |
| `0x002000-0x1FFFFF` | on metal: piscsi mailbox `+0x2000`, boot ROM `+0x6000`, bounce `+0x80000`. The stand-in does not model them: raw RAM or `0xFFFFFFFF` reads, *choice*; the driver never touches this range and never maps it to userland | `memorymap.h:36-38` |
| `0x200000-0x7FFFFFF` | **RTG RAM, direct** (`ABFLAG_RAM`); the framebuffer starts at `+0x200000`; the driver uses at most `+0x200000..+0x3000000` (46 MB) | `memorymap.h:18`, `uae_emulator.cpp:972,984-990`, `gfx.c:124,511` |

## 3. Register file — 32-bit words from `0x100` (`FW/Z3660/src/rtg/zzregs.h`)

Access width: the guest uses **aligned 32-bit** accesses only (the driver does). To be faithful
to the firmware's real-CPU path (`rtg.c:570-583`): a 16-bit write must be dispatched as the
whole aligned longword with the other half stale; a 16-bit read returns one lane. A model
that implements only `lget/lput` and treats `wput` that way is enough.

| Offset | Register | Read | Write |
|---|---|---|---|
| `0x100` | MODE | `0xFFFFFFFF` (write-only) | `mode = v & 0xff` (ignore `>= 23`), `colour = (v >> 8) & 0xf`, `scale = (v >> 12) & 0xf`; **an unchanged (mode, colour, scale) triple is a no-op**; a change reprograms the output: **the RTG display becomes visible on a MODE write** (there is no capture/switch register — see §5) — `rtg.c:1111-1132` |
| `0x104` | CONFIG | `0xFFFFFFFF` (**write-only**: `read_rtg_register()` has no case for it, `rtg.c:772-1027`) | store (interrupt enables on metal; the driver never writes it) |
| `0x110/0x114/0x118/0x11C` | X1 / Y1 / X2 / Y2 | `0xFFFFFFFF` | store |
| `0x120` | PAN | `0xFFFFFFFF` | `pan_offset = v + (X1 << cm) + Y1 * (X2 << cm)` with `cm` = the current COLORMODE (15-bit already remapped to 565); `pan_width = X2` — `rtg.c:1055-1066` |
| `0x124` | ROW_PITCH | `0xFFFFFFFF` | store (blitter only) |
| `0x128/0x12C` | X3 / Y3 | | store |
| `0x130` | RGB | | store (firmware byte-swaps it; blitter only) |
| `0x134/0x138` | FILLRECT / COPYRECT | | *choice*: implement per `rtg.c:1239-1284` or ignore — the driver never uses them |
| `0x140/0x144` | BLIT_SRC / BLIT_DST | | store |
| `0x148` | COLORMODE | `0xFFFFFFFF` | `cm = v & 0xf`; **3 (15-bit) is stored as 1 (565)** — `rtg.c:1080-1085` |
| `0x14C` | SRC_PITCH | | store |
| `0x17C` | VBLANK_STATUS | `0xFFFFFFFF` (the firmware's read case is commented out) | ignore |
| `0x188` | SET_SPLIT_POS | | store |
| `0x1A0` | FW_VERSION | **`0x00000103`** | ignore |
| `0x280` | FW_BETA | `0x00000015` (21) | ignore |
| `0x284` | FW_ALFA | `0x00000000` | ignore |
| `0x30C` | OP_CAPTUREMODE | `0xFFFFFFFF` | ignore (stub on the firmware, `rtg.c:1905-1908`) |
| everything else in `0x000-0xFFF` | | `0xFFFFFFFF` | ignore |

There is **no HW_VERSION** and nothing at `0x000-0x0FF`: a model that answered the ZZ9000's
`0x00`/`0xc0` there would make the driver's ZZ9000 rows attach to it, which must not happen.

## 4. Display

- Geometry from the mode id (`zz_video_modes.h:3-27`, timings `video.c:101-126`): ids 0-11 as
  the ZZ9000's (`zz9000.cpp:387-406`); 12-15 are 720x576/720x480 NS variants (never requested);
  **16 = 640x400, 17 = 1280x800, 18 = 1920x1200, 19 = 1600x900, 20 = 1680x1050, 21 = 1366x768**,
  22 = custom (treat as invalid). All 60 Hz.
- Pixel formats by `cm`: 0 = 8-bit CLUT (the driver sets no palette; *choice* to render as grey),
  1 = RGB565, 2 = 32-bit BGRA as the ZZ9000's, 3 → 1.
- Scanout: from RTG RAM at `+0x200000 + pan_offset`, `height` rows; **stride = mode width × bpp
  unless `pan_width != 0 && pan_width != width`, then `pan_width × bpp`** — `video.c:809-812`.
  This is the rule amiberry's `zz_set_pan` gets wrong for the ZZ9000 (`zz9000.cpp:965-975`): the
  driver writes X2 = 0 then PAN = 0 and expects the native stride. **The stand-in must not
  inherit that bug**; the pass criterion below depends on it.
- Output selection: the Z3660 has a single HDMI output owned by the RTG; the firmware shows its
  own splash until the guest's first MODE write (`video.c:245-270`). For the bench: show the
  native Amiga screen until the first valid MODE write, then the RTG framebuffer; there is **no
  register that switches back** (the driver's SETSWITCH is a no-op on this row). *Choice* how
  the operator returns to the native screen (a hotkey / IPC), the guest cannot.

## 4a. Rulings on the implementation's deviations (driver side, 2026-09-16)

The stand-in exists: amiberry fork (github.com/jusii/amiberry, not yet public) branch `amix-v830`, commit **814d25c** (`docs/z3660-rtg.md` there;
24 IPC-level assertions), scanout without the pan bug (fixed separately in 71e7527). The
amiberry side flagged five deviations; rulings:

| # | Deviation | Ruling |
|---|---|---|
| a | CONFIG `0x104` read back the stored value (as §3 said) although the firmware's `read_rtg_register()` has no case for it | **Match the firmware: reads `0xFFFFFFFF`.** §3 corrected above; the driver never reads it |
| b | Enable key `gfxcard_type=Z3660_RTG` + `gfxcard_size=128` on the existing RTG path | Accepted; recorded in §1 |
| c | `0x2000-0x1FFFFF` backed and logged memory | Accepted (it was the *choice* offered in §2) |
| d | Mode id 7 = 1920x1080 (the firmware's 50 Hz row) and ids 12-15 mapped to their NS geometries rather than refused | Accepted: the firmware accepts every id `< ZZVMODE_NUM`; the driver's table never requests 7 or 12-15 |
| e | Only FILLRECT/COPYRECT of the blitter modelled | Accepted: the driver uses no blitter register (dead code upstream) |

**Exact rig config for step 2:** the piscsi rig config with `chipset=aga` (**and** `chipset_compatible`
matching, per amix-kerntools' judgement), `gfxcard_type=Z3660_RTG`, `gfxcard_size=128`,
**no** `gfxcard_type=ZZ9000_*` line, `z3mapping` left at its default (the stand-in is not an
AutoConfig board; the piscsi model stays at `0x40000000`). Driver record:
`make=ZZ9K_TARGET=-DZZ9K_TARGET_Z3660,ZZ9K_BENCH=-DZZ9K_BENCH_Z3660` (one token per variable —
the Makefile carries `ZZ9K_BENCH` for exactly this), `cflags_strip=-fno-builtin` kept, new sha
pin, driver bytes as of **c5a5a46** (after 0fe840d the only delta is the Makefile's `ZZ9K_BENCH`).

## 5. What the bench must show (pass criteria)

Kernel and rig exactly as §4a. Evidence dir `<date>-zz9000-rig-step2/`, the report comes back
to this repo.

**Boot lines from driver bytes 7e5c0e8 onward (candidate 2):** on a rig where amiberry's
piscsi model AutoConfigures as `0x144B0001` at `0x40000000`, the autocon row matches it first,
is refused, and the fixed row then attaches the stand-in — **three lines, all expected**:
```
zz9000: Z3660 RTG at 0x40000000 size 0x8000000 is outside the identity map
zz9000: set autoconfig_rtg NO for AMIX
zz9000: Z3660 RTG (fixed base) at 0x10000000 size 0x2E00000 hw 0xFFFF fw 0x103
```
(Before 7e5c0e8 it was one line; the first two are the probe correctly declining the
emulator's Zorro III board and moving on. `-DZZ9K_BENCH_Z3660` is no longer needed — accepted
and ignored.)

1. Boot (bytes before 7e5c0e8): `zz9000: Z3660 RTG at 0x10000000 size 0x2E01000 hw 0xFFFF fw 0x103` (the kernel's
   `printf` prints upper-case hex) — exactly; no
   "autocon match", no "outside the identity map", no "no Z3660 RTG at".
0. Every test that maps the window runs with the process VM limit raised (`ulimit -v 131072`
   or more): `board_size` is 0x2E01000 (46 MB) and the shell default is 16 MB (bench step 1's
   finding). The same applies to the X startup path on metal — it must set the limit before
   starting `Xzz9000`; record in the report how it was set.
2. `zz9k_test /dev/zz9000 16 1920x1080`: the autocon table shows the ZZ9000-less rig (A2065,
   piscsi at `0x40000000`) and the tool's "No ZZ9000 autocon entry found / Asking the kernel
   driver anyway" note; then `zz9000: base 0x10000000 board 0x2e01000 fb 0x1000+0x2e00000 hw
   0xffff fw 0x103 mode 800x600 color 1`, `zz9000: zorro 3 product 0x01`, `fbinfo: 800x600 …
   offset 0x1000`, `Safe Z3660 register reads: FW_VERSION @0x01a0 = 0x0103, FW_BETA @0x0280 =
   0x0015`, `Requesting MODE write = 0x0105`, `MODE readback skipped (write-only on the Z3660)`,
   `Driver mode after SETMODE: mode 5 1920x1080 color 1`, then the bars at 1920x1080 on the
   RTG output **without any pitch workaround** (X2 = 0, PAN = 0 must give the native stride).
   The stand-in's log must show the writes at `0x100` (`0x00000105`), `0x148`, `0x120`, never
   any access below `0x100` or in `0x1000-0x1FFF`.
3. `zz9k_test /dev/zz9000 16 1280x800`: same with `MODE write = 0x0111` (id 17 is 1280x800
   here) and bars at 1280x800. `zz9k_test … 16 1920x800` must fail at SETMODE with `EINVAL`
   (no such mode on the Z3660).
4. `zz9k_test /dev/zz9000 console`: the framebuffer console at **1920x1080** (rows = 1080 /
   font height), scroll and cursor; "ZZ9000 console disabled" on return leaves the RTG output
   showing the last console frame (no switch-back exists).
5. mmap correctness: while the bars are held, `dd if=/dev/zz9000 bs=4096 skip=1 count=1 | od -x |
   head -2` shows framebuffer row 0 (device offset `0x1000` = guest `0x10200000`), and
   `dd if=/dev/zz9000 bs=4096 count=1 | od -x | head -2` shows the register page (`ffff` lanes
   for the write-only registers, `0103` at `0x1a2`). Reading at or past `0x2e01000` fails with
   `ENXIO` ("No such device or address"; `dd … | wc -c` shows 0 bytes) — upstream's bound.
   The stand-in's log must show **no access to guest `0x10001000-0x101FFFFF` from the mmap'd
   range** (the compacted device space skips it).
6. `zzpanfix /dev/zz9000 1920` (a ZZ9000-offset tool) must be harmless and irrelevant here:
   its SETREG pokes at `0x14/0x0a/0x0c` land in the unmapped `0x000-0x0FF` region; nothing
   changes on screen.
7. Evidence as for step 0 (`docs/BENCH-STEP0.md`), plus the stand-in's access log.

## 6. Out of scope for the stand-in

The piscsi mailbox / bounce inside the window (the rig's SCSI stays its own board), ethernet,
audio, soft3D, USB, the GFXData/DMA-op path, sprites, the firmware's own splash rendering.
