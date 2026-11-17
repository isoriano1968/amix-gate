# BENCH-STEP3.md — Z3660-row robustness on the stand-in (transcript-decidable)

> **Out-of-repo citations.** Evidence directories are cited by their date-stamped names. They, sibling repositories and archived images are kept outside this repo and are not needed to build or use the driver.

**Result of the first run (2026-09-16, kernel 40217 = c5a5a46, tools f5799f9):** R1, R4, R6, R7
PASS; R3 PASS by frame (its enable/disable strings are kernel-console printfs, not telnet
stdout — recipe corrected); R2's two "allowed large mode" cases could not run (the tool lacked
2560x1440 and 1920x1200 — added in ea4f546); R5 showed a read at `0x2E00000` (inside
`board_size 0x2E01000`) failing and a `dd seek=11793` write "succeeding" — **both measured through
the box's `dd`, whose skip/seek behaviour on a character device is unknown; the driver checks
both directions with the same expression**. R5 is re-specified below on `zz9k_rw` (one lseek + one
read/write, errno printed) and is **open** until run that way on a 6137966-or-later kernel. R6:
the tool's exit status is masked by `| tee`.

**Layout note (6137966 and later):** the Z3660 row's device space is the framebuffer alone
(`fb_offset 0`, `board_size 0x2E00000`); R5's register-page `dd` lines apply only to kernels
built from earlier bytes (40217 = c5a5a46). On later kernels `dd … count=1` reads framebuffer
row 0 and the EOF/ENXIO boundary is `0x2E00000` (skip 11776).

**For the amix-kerntools build rig.** Runs on the step-2 rig config (stand-in, `chipset=aga` +
`chipset_compatible=A3000`, no ZZ9000 line) and the **existing kernel 40217** (driver bytes
c5a5a46; the `zz9000.c` change in f5799f9 only names the console geometry it already had, so
40217 is the same driver). The tools come from **f5799f9** (`zz9k_reg` is new): build them on
the rig from that revision's `tools/` (or re-pin and rebuild the kernel — your call; both are
valid). Every check below is decided from the transcript; screenshots are optional except
where named. Run every mapping test under `ulimit -v 131072`.

Evidence dir `<date>-zz9000-rig-step3/`; the report comes back to this repo.

## R1 — mode switching back and forth (SETMODE path, no reboot)

Sequence, each a separate `zz9k_test` run held ~5 s (`sleep 5 |`):
```
zz9k_test /dev/zz9000 16 1920x1080   # MODE write = 0x0105, mode 5
zz9k_test /dev/zz9000 16 1280x800    # MODE write = 0x0111, mode 17
zz9k_test /dev/zz9000 16 800x600     # MODE write = 0x0101, mode 1
zz9k_test /dev/zz9000 16 1920x1080   # back
zz9k_test /dev/zz9000 32 1280x720    # 32-bit: MODE write = 0x0200, mode 0, color 2
zz9k_test /dev/zz9000 16 1920x1080   # back to 565
```
Expected per run: `Driver mode after SETMODE: mode <id> <w>x<h> color <c>`, `RTG test pattern
active`, bars at that geometry (one screenshot each for 1280x800 and 1280x720/32 is enough —
the 32-bit bars use the tool's `draw_bars32` colours). The stand-in's access log shows exactly
one `W 100 = 0000<mode>` per SETMODE and the zeroing writes (`110/114/118/11c/148/140/144/188/120`)
before it. `GETINFO` after each run (the tool prints it) reflects the new geometry.

## R2 — invalid and unsupported modes

```
zz9k_test /dev/zz9000 16 1920x800    # not on the Z3660: ZZ9KIOC_SETMODE: Invalid argument
zz9k_test /dev/zz9000 16 2560x1440   # id 11: 2560*1440*2 = 7.3 MB < fb_size → allowed; bars (tools from ea4f546 know the string)
zz9k_test /dev/zz9000 32 1920x1200   # 1920*1200*4 = 9.2 MB: allowed (fb 46 MB); mode 18 color 2
```
The first must fail exactly at SETMODE with EINVAL and leave the previous mode in force
(`GETINFO` via a following `zz9k_test … 16 800x600` run shows the old mode before its own
SETMODE — it prints `mode …` from GETINFO first).

## R3 — console enable/disable cycles

```
sleep 5 | zz9k_test /dev/zz9000 console      # enable + test page + disable
sleep 5 | zz9k_test /dev/zz9000 console      # again: same page, no leftovers from run 1
zz9k_test /dev/zz9000 consoletest            # test page on the already-disabled console: must re-enable it (ENXIO otherwise) and print the page
zz9k_test /dev/zz9000 noconsole              # "ZZ9000 console disabled"
sleep 5 | zz9k_test /dev/zz9000 16 1920x1080 # bars still work after console cycles
```
Console geometry on this build is 1920x1080 (`cols = 1920/8, rows = 1080/16` with sunfont).
The lines `zz9000 console enabled` / `zz9000 console disabled` are kernel `printf`s: they appear
on the kernel console (the VT / the RTG when redirected), **not** on telnet stdout. Decide R3 by
the frame and by the tool's exit status. Screenshot of the second run's page.

## R4 — GETREG/SETREG bounds and lane semantics (`zz9k_reg`, new)

```
zz9k_reg /dev/zz9000 get 0x1a2    # reg 0x01a2 = 0x0103   (FW_VERSION low lane)
zz9k_reg /dev/zz9000 get 0x1a0    # reg 0x01a0 = 0x0000   (high lane)
zz9k_reg /dev/zz9000 get 0x0000   # reg 0x0000 = 0xffff   (unmapped, high lane of 0xffffffff)
zz9k_reg /dev/zz9000 get 0x0ffe   # reg 0x0ffe = 0xffff   (last lane inside reg_size)
zz9k_reg /dev/zz9000 get 0x1000   # ZZ9KIOC_GETREG: Invalid argument   (reg_size bound = the test area is unreachable)
zz9k_reg /dev/zz9000 get 0x1a1    # ZZ9KIOC_GETREG: Invalid argument   (odd offset)
zz9k_reg /dev/zz9000 set 0x118 0x0000 ; zz9k_reg /dev/zz9000 set 0x11a 0x0780
                                  # HI latch then LO write: the access log shows ONE 4-byte write W 118 = 00000780 (X2 = 1920), nothing at 0x11a
zz9k_reg /dev/zz9000 set 0x11a 0x0000
                                  # LO write with an empty latch: W 118 = 00000000
```

## R5 — read/write bounds (re-specified on `zz9k_rw`; for a 6137966-or-later kernel, `board_size 0x2E00000`)

```sh
zz9k_rw /dev/zz9000 read 0 0x1000            # read 0x0 len 0x1000: 4096 bytes … (framebuffer row 0 on this layout)
zz9k_rw /dev/zz9000 read 0x2dff000 0x1000    # 4096 bytes (the last page)
zz9k_rw /dev/zz9000 read 0x2dff000 0x2000    # 4096 bytes (a transfer that would cross the end is CUT to the window)
zz9k_rw /dev/zz9000 read 0x2e00000 0x1000    # errno 6 (ENXIO: at board_size)
zz9k_rw /dev/zz9000 read 0x2e01000 0x1000    # errno 6
zz9k_rw /dev/zz9000 write 0 0x1000 0         # 4096 bytes (row 0 black if a mode is up)
zz9k_rw /dev/zz9000 write 0x2dff000 0x2000 0 # 4096 bytes (cut to the window)
zz9k_rw /dev/zz9000 write 0x2e00000 0x1000 0 # errno 6
zz9k_rw /dev/zz9000 write 0x2e01000 0x1000 0 # errno 6
```
The driver's rule (`zz9000rw`): offset ≥ `board_size` → ENXIO for both directions; a transfer
that would cross the end is shortened to the window. Any other result is a driver finding —
report the exact line.

## R6 — the VM limit path (tool behaviour)

Without `ulimit`: `zz9k_test /dev/zz9000 16 1920x1080` → `mmap: Not enough space`, then the
tool prints the hint (`Cannot map 0x2e01000 bytes … raise the process VM limit first (e.g.
ulimit -v 131072); the default is 16 MB.`), issues `SETSWITCH CAPTURE` (a no-op on this row —
the RTG keeps showing the last frame; that is expected, §3.5 of the stand-in contract) and
exits 1. Then with `ulimit -v 131072` the same command succeeds.

## R7 — repeated probe / open

`for i in 1 2 3 4 5; do zz9k_reg /dev/zz9000 get 0x1a2; done` — five identical lines, no
re-probe messages on the kernel console (the driver keeps `bp->base`), and the stand-in log
shows only the five reads at `0x1a0`.

## Pass criterion

Every listed line as given; no kernel message other than the driver's own console
enable/disable lines; the stand-in log never shows an access below `0x100` or in
`0x1000-0x1FFF`. Anything else is a finding: record the exact line and stop that R-case
(the others are independent).
