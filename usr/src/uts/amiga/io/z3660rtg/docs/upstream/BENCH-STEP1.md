# BENCH-STEP1.md — the 32-bit-window rehearsal on amiberry's ZZ9000 Zorro III model

> **Out-of-repo citations.** Evidence directories are cited by their date-stamped names. They, sibling repositories and archived images are kept outside this repo and are not needed to build or use the driver.

**Result: PASS, 2026-09-16** (kernel 40227; evidence `2026-09-16-zz9000-rig-step1/`,
report `31-report.md`; verified here). **One finding, not in the driver:** T1's
`mmap(board_size)` failed with ENOMEM under the login shell's default per-process VM limit
(`ulimit -a` memory(kbytes) 16384 soft, hard unlimited); the size probe `23-mmap-size-probe.txt`
fails at every size ≥ 16 MB and succeeds ≤ 12 MB, and under `ulimit -v 131072` the 47 MB map
succeeds and T1 is as specified (bars pixel-identical to step 0). The kernel maps the window;
the pass criterion counts the `ulimit` run. Consequence recorded in DESIGN.md §8 (runtime
contract for `Xzz9000`) and in `BENCH-STEP2-STANDIN.md`. Cosmetic: the kernel's `printf`
prints upper-case hex (`size 0x2E10000`); and the tool at e05d4da left the display on the
RTG screen after the mmap failure — fixed in the following tool commit.

**For the amix-kerntools build rig.** Bench step 1 of DESIGN.md §6. Same shape as
`docs/BENCH-STEP0.md`; the driver side never builds the kernel or drives the rig
and verifies this report. Step 0 (PASS 2026-09-16, kernel 20408) is the baseline.

## What it proves

Everything the Z3660 row needs from the *kernel* that step 0 could not show on a 4 MB Zorro II
window at `0x200000`: kernel-side dereference of a board window above 16 MB inside the
identity-mapped low 1 GB (console, FILL, read/write), user `mmap()` of such a window through
the stock 2 KiB-page `phystopfn`, the row's `fb_max` cap on `board_size`, and the probe's
"outside the identity map" refusal. It uses amiberry's ZZ9000 **Zorro III** model (product 4,
128 MB, `er_type 0x83`, same 16-bit register file and `+0x10000` framebuffer as the Z2 model —
`amiberry src/gfxboard.cpp:444-455`, `src/zz9000.cpp:26-28`) placed **inside the identity map**
by `z3mapping=uae` (`src/expansion.cpp:296-308`: Zorro III boards are then laid out from
`0x10000000`). The driver row that accepts it exists only in a bench build: `-DZZ9K_BENCH_Z3`
replaces upstream's product-4 rejection with an accept-if-inside-the-identity-map row
(`zz9000.c`, `ZZ9K_BENCH_Z3`). Not a supported configuration; a bench aid (DESIGN.md §9.5).

## What to build

| Item | Value |
|---|---|
| Source | `amix-z3660rtg`, branch `z3660-target`, driver bytes as of **e05d4da** (the subtree is unchanged by the later docs commits; the harness exports the named revision) |
| chardevs record | a **second record or a `CHARDEVS_CONF=` work conf** (one record per build): same subtree path, `git=e05d4da`, new `sha256=` pin, `cflags_strip=-fno-builtin` kept, console OFF, and **`make=ZZ9K_TARGET=-DZZ9K_BENCH_Z3`** (as run; from the Makefile with `ZZ9K_BENCH` onward the bench aid goes in `make=ZZ9K_BENCH=-DZZ9K_BENCH_Z3`) |
| Driver compile line on the box | must show `-D_KERNEL -DSVR40 -DSVR4 -DZZ9K_BENCH_Z3 -I../../.. -I../../inc` and no `-fno-builtin` |
| Kernel | golden 030 build, D245 clean-gate STABLE, as step 0 |
| Rig config | `configs/amix-z3660-piscsi-rig-zz9000.uae` with three changes: `gfxcard_type=ZZ9000_Z3`, `gfxcard_size=128`, **`z3mapping=uae`**. Everything else unchanged (`chipset=ecs` is fine: no fixed row is compiled in) |

**Side effect to expect and record:** `z3mapping=uae` moves *every* Zorro III board below
`0x40000000`, including the rig's Z3660 piscsi model (128 MB). amiberry's boot log lists the
new bases; the two Z3 boards land at `0x10000000` and `0x18000000` in amiberry's expansion
order (record which is which). The piscsi driver in the rig kernel then takes its direct-map
arm instead of `sptalloc` (`amix-z3660scsi/src/z3660.c:711-745`) — the rig must still boot
from it. If it does not, that is a rig/piscsi finding, not this driver's; stop and report.
`debugmem` (32 MB at `0x08000000`) does not collide.

## Host-side oracle

As step 0, on the e05d4da subtree with the bench define:
```sh
m68k-cbm-sysv4-gcc -O -traditional -fno-builtin -Dinline=__inline__ -D_KERNEL -DSVR40 -DSVR4 \
    -DZZ9K_BENCH_Z3 -I../../.. -I../../inc -c zz9000.c -o /tmp/x.o
```
`U` and `T` sets identical to upstream 75e2449; `-Wall` the same five warnings.

## On the rig — expected output

Let `BASE` be the ZZ9000 Z3 model's base from amiberry's log (`0x10000000` or `0x18000000`).

Boot lines (the row's own wording; **no** "unsupported", **no** "outside the identity map"):
```
zz9000: autocon match pc 0x6D6E0004 base 0x<BASE> size 0x8000000
zz9000: Zorro III board (bench) at 0x<BASE> size 0x2e10000 hw 0x0 fw 0x207
```
`size 0x2e10000` = `fb_offset 0x10000 + fb_max 0x2e00000`: the reported window is capped,
not the 128 MB AutoConfig size. `mkdev.sh` → 49,0. Tools built on the rig from the e05d4da
sources, zero diagnostics.

### T1 — `ulimit -v 131072; zz9k_test /dev/zz9000 16 1920x800`

Run T1 (and any test that maps the window) with the process VM limit raised to at least
`board_size` + the tool's own footprint; the rig shell's default of 16 MB refuses a 47 MB map
with `mmap: Not enough space`.

Autocon table: the A2065, the piscsi model and the ZZ9000 Z3 (`6d6e 04 83 … <BASE> 08000000
MNT ZZ9000-Z3 unsupported-on-AMIX` — the tool's upstream label, cosmetic), both Z3 boards
below `0x40000000`. Then the tool's `Only ZZ9000-Z3/product 4 is present. AMIX driver requires
Z2/product 3.` / `Asking the kernel driver anyway …` note, and:
```
zz9000: base 0x<BASE> board 0x2e10000 fb 0x10000+0x2e00000 hw 0x0 fw 0x207 mode 800x600 color 1
zz9000: zorro 3 product 0x04
fbinfo: 800x600 depth 16 bpp 2 stride 1600 offset 0x10000 size 0x2e00000
fbinfo: masks r 0xf800 g 0x7e0 b 0x1f a 0x0 shifts 11/5/0/0
Safe ZZ9000 register reads:  HW_VERSION 0x0000, FW_VERSION 0x0207, MODE 0x0000, CONFIG 0x0000, VCAP_MODE 0x0000
Requesting MODE write = 0x0111 (1920x800 color 1)
MODE readback after SETMODE = 0x0111
Driver mode after SETMODE: mode 17 1920x800 color 1
RTG test pattern active. Press return to restore capture:
```
**The decisive line is the absence of `mmap: …`** (with the limit raised): `zz9k_test` maps
`board_size` (0x2e10000, 47 MB, 23 584 pages of 2 KiB) at `BASE` and draws through it. Without
the raised limit the expected failure is `mmap: Not enough space`, after which the tool (from
the commit after e05d4da) restores the native display and prints the `ulimit -v` hint. Screenshot: the bars, expected
pixel-identical to step 0's `13-t1-rtg-1920x800-16-bars.png` (same renderer). amiberry's
pan-pitch bug still applies — `zzpanfix` as before. Native console restored on exit.

### T2 — `zz9k_test /dev/zz9000 console`

The framebuffer console at 1920x800 drawn by the *kernel* through `bp->fb = BASE + 0x10000`:
expected pixel-identical to step 0's `18-t2-rtg-console.png`. Restore on exit.

### T3 — `zz9k_test /dev/zz9000 blit 1920x800`

As step 0 (navy background, three rects, the copy at (960,400)); the FILL ioctl writes the
window from kernel mode. Restore on exit.

### T4 — read(2) across the seam and at the cap

While T1 holds the bars:
```sh
dd if=/dev/zz9000 bs=65536 skip=1 count=1 2>/dev/null | od -x | head -2    # 0000 … (row 0, black)
dd if=/dev/zz9000 bs=65536 skip=9 count=1 2>/dev/null | od -x | head -2    # f800 … (row 136, red)
dd if=/dev/zz9000 bs=4096 skip=15 count=2 2>/dev/null | wc -c              # 8192 (across 0x10000)
dd if=/dev/zz9000 bs=65536 skip=736 count=1 2>/dev/null | wc -c            # 65536 (last 64 KB below the cap)
dd if=/dev/zz9000 bs=65536 skip=737 count=1 2>/dev/null | wc -c            # 0 (EOF at 0x2e10000, not at 128 MB)
```

### T5 — `zzpanfix /dev/zz9000 1920`

Works as before (16-bit SETREG on this row).

### T6 — the refusal path (negative test, one reboot)

Set `z3mapping=real` (the ZZ9000 Z3 lands at `0x40000000`, the piscsi model at `0x48000000`
or the reverse), rebuild nothing, reboot the same kernel. Expected boot line:
```
zz9000: Zorro III board (bench) at 0x40000000 size 0x8000000 is outside the identity map
```
and `zz9k_test` fails at `ZZ9KIOC_PROBE` with `ENXIO` ("No supported board was found"). The
rig must still boot (the piscsi driver takes its `sptalloc` arm again, as in step 0). Restore
`z3mapping=uae` afterwards or drop the step-1 config entirely; step 0's config stays the
reference rig.

## Pass criterion

Boot lines as listed with a `BASE` below `0x40000000`; T1 transcript as listed (no `mmap`
error), bars and console screenshots pixel-identical to step 0's; T3 as step 0; T4 values as
listed; T5 works; T6's refusal line and `ENXIO`; clean-gate STABLE; tools zero diagnostics.
Any `mmap` failure, a bus error, or a hang at boot is the finding this step exists to expose:
record it exactly and stop. If T1 fails *only* at `mmap` (47 MB = 23 560 PTEs on the 2 KiB
kernel), ask the driver side for a diagnostic revision with a smaller `ZZ9K_Z3660_FB_MAX` (e.g.
16 MB) to separate "windows above 16 MB cannot be mapped" from "this many pages cannot";
the metal kernel maps the same 46 MB in half the pages.

## Evidence to keep

Under `<date>-zz9000-rig-step1/`: the record line used, build log with the compile
line, gate sum, amiberry's board list with the bases, boot screenshot(s) for T1-T5 and T6,
transcripts, `od` outputs, screenshots, `git -C amix-z3660rtg rev-parse z3660-target`.
