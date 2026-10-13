# BENCH-STEP0.md — regression of the board-table refactor on the ZZ9000 rig

> **Out-of-repo citations.** Evidence directories are cited by their date-stamped names. They, sibling repositories and archived images are kept outside this repo and are not needed to build or use the driver.

**Result: PASS, 2026-09-16** (kernel 20408; every criterion met, nothing adapted; evidence
`2026-09-16-zz9000-rig-step0/`, report `27-report.md`; verified here:
T1 transcript diff = the allowed lines only, T4 values exact, T1/T2 screenshots byte-identical
to the 2026-09-15 lane, `-Wall` sets identical). Kept as the record of what was run.

**For the amix-kerntools build rig.** Bench step 0 of DESIGN.md §6: prove that the refactored
driver (branch `z3660-target`, default rows, no `make=`) behaves exactly like upstream 75e2449
did in the 2026-09-15 lane (kernel 09684, evidence `2026-09-15-zz9000-rig/`). The
driver side never builds the kernel or drives the rig; it verifies this report.

## What to build

| Item | Value |
|---|---|
| Source | this repo (`amix-z3660rtg`), branch **`z3660-target`**, driver bytes as of **6e0acde** (branch head e192d6b or later; the default build is unchanged by later docs-only commits) |
| Subtree | `overlay/usr/sys/amiga/driver/zz9000` (same layout as upstream: `zz9000.c`, `zz9000.h`, `Makefile`, `mkdev.sh`, `tools/`, `init/`) |
| chardevs record | the existing `chardev zz9000 …/amix-z3660rtg/overlay/usr/sys/amiga/driver/zz9000 49 zz9000init zz9000open,zz9000close,zz9000read,zz9000write,zz9000ioctl,zz9000mmap` with **`cflags_strip=-fno-builtin` kept**, **no `make=`**, `git=6e0acde`, and a **new `sha256=` pin** over the tree (the bytes changed: `zz9000.c`, `zz9000.h`, `Makefile`, `tools/zz9k_test.c`) |
| Console hook | OFF, as before (`console=` absent) |
| Rig config | `configs/amix-z3660-piscsi-rig-zz9000.uae` unchanged: `gfxcard_type=ZZ9000_Z2`, `gfxcard_size=4` |
| Kernel | golden 030 build, same recipe as the 2026-09-15 lane; the D245 clean-gate must pass (two builds, equal `sum -r`) |

The generated driver rule must read `cd zz9000; $(MAKE)` with nothing appended; the driver's
compile line on the box must show `-D_KERNEL -DSVR40 -DSVR4  -I../../.. -I../../inc` (two
spaces where the empty `$(ZZ9K_TARGET)` expands) and **no `-fno-builtin`** (stripped).

## Host-side oracle (before touching the box, 1 minute)

From the kerntools cross toolchain, compile the driver object as the driver side
did and compare with the upstream object:

```sh
# upstream (main = 75e2449 bytes) and z3660-target, each in a copy laid out as
# /usr/sys/amiga/driver/zz9000 beside the stock console/ and inc/ dirs
m68k-cbm-sysv4-gcc -O -traditional -fno-builtin -Dinline=__inline__ -D_KERNEL -DSVR40 -DSVR4 \
    -I../../.. -I../../inc -c zz9000.c -o /tmp/x.o        # always pass -o (wrapper hazard)
m68k-cbm-sysv4-nm /tmp/x.o | grep ' U '   # must be exactly:
#   autocon bootinfo builtinfont conputc copyin copyout printf uiomove
m68k-cbm-sysv4-nm /tmp/x.o | grep ' T '   # must be exactly the nine zz9000* entry points
```
The driver side's result: identical `U` and `T` sets for upstream and 6e0acde; `-Wall` shows the
same five warnings on both sides (the stock header's "`/*' within comment" from `sys/types.h`,
implicit `printf`/`autocon`, unused `zz9k_blit_fill`/`_copy`).

## On the rig — expected output, line by line

Boot (compare with `19-rig-boot-zz9000-kernel.png`): the two driver lines must be **textually
identical** to 09684's:
```
zz9000: autocon match pc 0x6D6E0003 base 0x200000 size 0x400000
zz9000: Zorro II board at 0x200000 size 0x400000 hw 0x0 fw 0x207
```
(no "outside the identity map", no "did not look like", no "no … at").

`sh mkdev.sh` → `crw-rw-rw- … 49, 0 /dev/zz9000`. Build the tools on the rig from the pushed
sources (`cd tools; make`) — zero diagnostics expected under GNU GCC 1.40.5, as before.

### T1 — `zz9k_test /dev/zz9000 16 1920x800` (compare with `22-zz9k_test-1920x800.txt`)

Same autocon table (three boards). Then, in this order:
```
zz9000: base 0x200000 board 0x400000 fb 0x10000+0x3d0000 hw 0x0 fw 0x207 mode 800x600 color 1
zz9000: zorro 2 product 0x03                      <- NEW line (tool prints GETINFO identity)
fbinfo: 800x600 depth 16 bpp 2 stride 1600 offset 0x10000 size 0x3d0000
fbinfo: masks r 0xf800 g 0x7e0 b 0x1f a 0x0 shifts 11/5/0/0
Safe ZZ9000 register reads:
  HW_VERSION    @0x0000 = 0x0000
  FW_VERSION    @0x00c0 = 0x0207
  MODE          @0x0002 = 0x....                  <- whatever the card holds before SETMODE
  CONFIG        @0x0004 = 0x0000
  VCAP_MODE     @0x000e = 0x0000

Requesting MODE write = 0x0111 (1920x800 color 1)
MODE readback after SETMODE = 0x0111
Driver mode after SETMODE: mode 17 1920x800 color 1
RTG test pattern active. Press return to restore capture:
```
Screenshot while the pattern is held (nohup + `sleep N |` as in the lane): must equal
`25-rtg-1920x800-16-bars.png` (Germany/Spain bars; pixel-check six rows). After return: native
console restored (`26-native-console-restored.png` equivalent). Note the amiberry pan-pitch bug
still applies: use the same `zzpanfix` workaround as the lane if the screen is black after
SETSWITCH.

Note: the 2026-09-15 transcript printed `mode 1920x800` in the first `zz9000:` line because it
was captured after an earlier SETMODE in the same boot; on a fresh boot it reads `800x600`.
Either is fine — the field is the driver's current mode, not a regression signal.

### T2 — `zz9k_test /dev/zz9000 console` (compare with `27-rtg-console-b.png`)

`ZZ9000 console enabled` on the serial/telnet side; the framebuffer console at **1920x800**
with sunfont glyphs, the colour words, the cursor-movement and clear-to-eol lines, 35+ scrolled
rows and the cursor. Return → `ZZ9000 console disabled`, native console back.

### T3 — `zz9k_test /dev/zz9000 blit 1920x800`

Navy `0x0010` background (the tool's FILLRECT after its FILL 0), red/yellow/green 640x200 rects
at (80,80)/(160,180)/(240,280), the (80,80) region copied to (960,400), restore on return. (CPU paths; exercises FILL/FILLRECT/COPYRECT via
`bp->fb`.)

### T4 — read(2) across the register/framebuffer seam (NEW check, the `zz9k_devphys` path)

While T1 holds the bars (top sixth of 800 rows = rows 0-132 black `0x0000`, rows 133-265 red
`0xf800`, stride 3840):
```sh
dd if=/dev/zz9000 bs=65536 skip=1 count=1 2>/dev/null | od -x | head -2
#   0000000 0000 0000 0000 0000 ...          (framebuffer row 0, black)
dd if=/dev/zz9000 bs=65536 skip=9 count=1 2>/dev/null | od -x | head -2
#   0000000 f800 f800 f800 f800 ...          (offset 0x90000 = fb + 0x80000 = row 136, red)
dd if=/dev/zz9000 bs=4096 skip=15 count=2 2>/dev/null | wc -c
#   8192                                      (0xF000..0x10FFF spans the seam; no error)
dd if=/dev/zz9000 bs=65536 skip=64 count=1 2>/dev/null | wc -c
#   0                                         (EOF at board_size 0x400000, as before)
```

### T5 — GETREG/SETREG bound unchanged

`bench/zzpanfix.c` still works: `zzpanfix /dev/zz9000 1920` → `zzpanfix: X2=1920,
PAN=0 re-issued`, and the bars appear at once (a SETREG at 0x14/0x0a/0x0c on a 16-bit row).

## Pass criterion

All of: boot lines identical; T1 transcript equal to the lane's modulo the one NEW `zorro 2
product 0x03` line and the pre-SETMODE `mode`/MODE fields; T1 bars and T2 console screenshots
match the lane's; T4 values as listed; tools compiled with zero diagnostics on the rig; clean-gate
STABLE. Anything else is a regression: report the exact line and stop (the refactor is meant
to be invisible on this rig).

## Evidence to keep

Under `<date>-zz9000-rig-step0/`: build log (with the driver compile line), clean-gate
sum, boot screenshot, T1/T2/T3 transcripts and screenshots, T4 `od` output, the chardevs record
line used (with its sha pin), `git -C amix-z3660rtg rev-parse z3660-target`.
