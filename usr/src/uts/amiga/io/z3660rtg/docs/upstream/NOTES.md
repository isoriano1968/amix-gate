# NOTES.md — amix-z3660rtg running log

> **Out-of-repo citations.** Evidence directories are cited by their date-stamped names. They, sibling repositories and archived images are kept outside this repo and are not needed to build or use the driver.

Newest entries at the bottom. Dates are absolute.

## 2026-09-14 — fork + brief

- Repo forked from isoriano1968/zz9000-amix at 75e2449 (MIT).
  `upstream` is fetch-only (push URL disabled); no `origin`. Branch `main` (upstream's name —
  kept, per the project rule not to rename an existing default branch).
- Brief: in `2026-09-15-z3660-rtg/`; gathered inputs:
  `INPUTS-2026-09-16.md` (bench + firmware-source facts). `FIRMWARE-RTG-INTERFACE.md`
  (Z3660 firmware side) not yet merged as of this entry.
- Binding project facts added: demo machine is a real
  68LC060 (no FPU) at 128 MB; RTG driver bench-first on emulation, metal only after
  2026-09-19; the Z3660 identity/windows go behind ONE switch (upstreamable to isoriano);
  an adversarial check of the register-compatibility table before DESIGN.md is committed.

## 2026-09-15 — driver read-through (for DESIGN.md)

- Read end to end: zz9000.h (199 lines), zz9000.c (1455), integration/*.txt, Makefile,
  mkdev.sh, init/S99zz9000, tools/{Makefile,zz9k_test.c,zz9k_console,zz9k_colorls_env},
  colorls.c skimmed (pure userland, no board knowledge), README/INSTALL/CHANGELOG.
- Findings that shape the design:
  * `zz9k_blit_fill` / `zz9k_blit_copy` (hardware blitter) are defined but NEVER called —
    FILLRECT/COPYRECT ioctls and the console all use CPU framebuffer writes. The Z3660's
    blitter compatibility is therefore not load-bearing for a first driver.
  * `ZZ9K_FB_OFFSET` is consumed at 10 sites in zz9000.c (fbinfo, attach arithmetic,
    cpu fill/copy, FILL ioctl, GETINFO, console fill/draw/cursor/scroll) and 2 in
    zz9k_test.c — the natural seam is a per-board `fb_offset` field.
  * `zz9k_present()` accepts a board when NOT (hw in {0,0xffff} AND fw in {0,0xffff});
    amiberry's ZZ9000 returns hw 0x0 fw 0x207, so the check rides on FW_VERSION alone.
  * `zz9k_test` gates all tests on an autocon entry for 0x6d6e/3 (`found & 1`) and
    mmaps the WHOLE `info.board_size` — both identity-dependent.
  * Console renderer is hard-wired to 1920x800 / mode id 17 / RGB565.
- Bench evidence consumed (`2026-09-15-zz9000-rig/`, kerntools 00c316c): upstream
  75e2449 builds with the golden's GNU GCC 1.40.5 once the Makefile's gcc-2 `-fno-builtin`
  is stripped (kerntools `cflags_strip`); kernel 09684 boots in the piscsi rig; driver log
  `autocon match pc 0x6D6E0003 base 0x200000 size 0x400000 hw 0x0 fw 0x207`; zz9k_test
  1920x800 RGB565 bars (25-*.png, pixel pattern verified by eye here) and the framebuffer
  console (27-*.png). bootinfo.autocon[]: ZZ9000 Z2 0x6d6e/3 at 0x200000 4 MB; A2065; Z3660
  piscsi Z3 (type 0x83) 0x144b/1 at 0x40000000 128 MB.
- amiberry finding (not a driver bug): zz_set_pan takes pitch from blitter X2 at every
  PAN_LO write; the driver's SWITCH_RTG writes PAN=0 with X2==0 → pitch 0 → black. The
  ZZ9000-derived firmware treats X2==0 as "mode width" (Z3660 video.c:810). Fix pending in
  the amiberry fork; bench-proven workaround `bench/zzpanfix.c` (SETREG X2=width, PAN=0).
- Read-only survey: sibling drivers' probe/map idioms;
  grimoire facts; Z3660 firmware RTG source (preliminary — the Z3660 firmware side's
  FIRMWARE-RTG-INTERFACE.md supersedes it); amiberry ZZ9000/Z3660 emulation for the bench
  plan. Results folded into DESIGN.md.
- Survey results (all folded into DESIGN.md with citations):
  * Siblings: `Z3660_PROD 0x144B0001`, `Z3660_FIXED 0x10000000`; AGA gate `VPOSR >= 0x22`;
    direct-map when `base + window <= VSECT1 (0x40000000)`, else `sptalloc` (pool 4 MB);
    32-bit volatile MMIO only; on 040/060 `DTT0 = 0x003fc060` cache-inhibits the low 1 GB
    (`amix-z3660scsi/src/z3660.c:640-745`). d90f5fd's GNU inline asm is what GCC 1.40.5
    rejects — never add asm to this driver.
  * Grimoire: `autocon()` matches any (mfg,product) incl. Zorro III; the wall is the
    identity map (`zorro-autoconfig.md:52-72`); Amix 2.1's `bootinfo.autocon[]` can miss the
    Z3660 under both `autoconfig_rtg` settings; the X server's contract (`x11r63-zz9000.md`).
  * X server source (`2026-09-14-isoriano-repos/x11r6.3-amix/…/zz9000hw.c:114-136`):
    `mmap(0, info.board_size)` then `fbBase = base + fbi.fb_offset` — the driver alone defines
    the window shape. Verified by reading the file.
  * amiberry: ZZ9000 Z2 and Z3 models share one 16-bit register map (`docs/zz9000.md`);
    `gfxcard_type=ZZ9000_Z3 gfxcard_size=128 z3mapping=uae` puts a 128 MB ZZ9000 at
    `0x10000000`; the fork has NO Z3660 RTG or ethernet model, only piscsi; a fixed-base
    stand-in has the `debugmem_bank` precedent; the `zz_set_pan` bug is unfixed.
  * 040/060 port: Model-B = 4 KiB pages; stock headers inline `phystopfn` as `pa >> 11` →
    `mmap` at 2×pa on that kernel (`include-modelb/sys/immu.h`, `src/z3660_modelb.py`);
    `devmaptest` exists; RTG kernels with VA2000/Xsvga are precedent for `cdevsw`+`mmap`.

## 2026-09-15 — the register-file finding (verified in source, not from the reader alone)

- The Z3660 RTG is NOT byte-compatible with the ZZ9000 register map the upstream driver
  speaks. `FW/Z3660/src/rtg/zzregs.h:28-78`: 32-bit-word registers from 0x100 (MODE 0x100,
  PAN 0x120 single 32-bit, FW_VERSION 0x1A0 = 0x0103, no HW_VERSION, capture stub at 0x30C);
  `DRV/rtg/gfx.c:211-212,485` `registers[(reg)>>2]`; framebuffer at +0x200000
  (`memorymap.h:18`, `gfx.c:124,504`), 46 MB usable (`gfx.c:511`); mode 17 = 1280x800
  (`zz_video_modes.h:21`), 18-22 added; real-CPU path re-reads the aligned 32-bit word
  (`rtg.c:557-583`) → 32-bit accesses only; 0x1000-0x1FFF is a firmware test area.
  Semantics (MODE formula, colour modes, X2-as-pitch, blitter) are the ZZ9000's.
- Consequence: the "single switch" carries a register map + width, mode table, presence
  test and capture semantics, not just identity/window. Still one table row + one accessor.
- Proposed: compacted device space on the Z3660 (`fb_offset 0x2000`, `fb_phys 0x200000`) so
  `mmap(board_size)` never exposes the piscsi mailbox/bounce to userland.
- DESIGN.md written (§0-§10); an independent check of §3 (read-only) before
  commit.
- Independent check result: 15/16 claims CONFIRMED; one WRONG — under
  `autoconfig_rtg YES` the product byte is **0x01** (`cpu_emulator.cpp:338-345`, FPGA
  `z3660.v:981`, `docs/real-cpu-autoconfig-and-ext-rom.md:21`), so Q9 is closed and the
  sibling/amiberry `0x144B0001` is right. Precision fixes applied: registers end at 0x89C;
  the write path re-reads at the raw address (`rtg.c:570-580`) and aligns in
  `write_rtg_register` (`:1042`), size bits ignored; a 16-bit read gets one lane; MODE,
  COLORMODE, X1.., ROW_PITCH, VBLANK are write-only (reads 0xFFFFFFFF); unchanged-triple MODE
  writes are no-ops (`:1120-1123`); RGB is byte-swapped (`:1214`); palette equivalents are
  `REG_ZZ_OP_DATA 0x300`/`REG_ZZ_OP 0x304`; the AmigaOS driver's presence test is
  `FW_VERSION >> 8 == 1` (`gfx.c:488-495`) — adopted for the Z3660 row.
- Project decisions recorded in DESIGN.md §9 (all seven). Next, in order, separate
  commits: (a) `docs/FIRMWARE-RTG-INTERFACE.md` question sheet for the Z3660 firmware side;
  (b) the board-table refactor on branch `z3660-target`, default rows only, upstream
  behaviour preserved; (c) a build-and-selftest recipe for the kerntools ZZ9000 rig (bench
  step 0) — the kernel is built by amix-kerntools, never here.
- 9d8c09f + 61c7e3d verified. Z3660 firmware side will answer the question sheet via
  `2026-09-15-z3660-rtg/FIRMWARE-ANSWERS.md` (last in its queue) and hand it back
  here to merge into `docs/FIRMWARE-RTG-INTERFACE.md`.
- TOOLCHAIN WARNING (2026-09-15): the installed cross wrapper at
  `~/opt/amix-cross` has a fixed-but-not-reinstalled defect — `-c` on a `.s`/`.S` with no
  explicit `-o` overwrites the SOURCE with the object. Until the wrapper is reinstalled: always pass
  an explicit `-o`, never feed `.i` inputs. (This repo does not build kernels; it matters
  for any host-side compile check of the driver.)
- Design review before touching driver source (branch `z3660-target`).
- kerntools contract (c011498, unreviewed): chardevs record repointed to
  `overlay/usr/sys/amiga/driver/zz9000` (same bytes/pin); the target build is
  a `make=ZZ9K_TARGET=-DZZ9K_TARGET_Z3660` pass-through on the record → `cd zz9000; $(MAKE)
  ZZ9K_TARGET=…`. Our Makefile must honour `ZZ9K_TARGET` (default empty) in CFLAGS; bench aid
  = `ZZ9K_TARGET=-DZZ9K_BENCH_Z3`. Recorded in DESIGN.md §4.2/§6/§9.

## 2026-09-15 — Z3660 firmware side's answers merged

- `2026-09-15-z3660-rtg/FIRMWARE-ANSWERS.md` (source-only, branch amix-main-2) copied
  verbatim to `docs/FIRMWARE-ANSWERS-2026-09-15.md`; condensed answers merged into the slots of
  `docs/FIRMWARE-RTG-INTERFACE.md`. Q1, Q2(a,b,d), Q4, Q6 (firmware side), Q7, Q8, Q10, A3, A5
  CLOSED; Q3/A4 (no-board read: bus error vs open bus) UNSETTLED — bus/FPGA behaviour; A1/A2/A6
  METAL. Every answer corroborates DESIGN.md §3; no contradiction found.
- Refinements taken into DESIGN.md: the `0x1000-0x1FFF` printf is unconditional → the Z3660
  row's device space and GETREG bound end at 0x1000 (`fb_offset 0x1000`, new row field
  `reg_size`); the HDMI is the firmware's own splash → `SETSWITCH CAPTURE` is a no-op by
  construction; ethernet/audio offsets are guest-relative in the top 32 MB; A3 = 0x50000000
  with the RAM board. Never probe `+0x000`; `0xFFFFFFFF` never counts as present.
- The Z3660 firmware side's emulation verdict matches the design: a two-backend driver (rows with
  register map + width) and, for the bench, an amiberry Z3660-RTG model implementing the 0x100
  map (bench step 2) — the ZZ9000 emulation alone cannot exercise the Z3660 row.

## 2026-09-15 — refactor implemented on branch `z3660-target`

- d5671a7: board table + accessor + probe + devphys + console geometry from the row; default
  rows only (ZZ9000 Z2 accept / Z3 reject, upstream messages verbatim); Makefile `ZZ9K_TARGET`
  hook; zz9k_test's autocon gate advisory + prints zorro/product.
- 17dc94e: docs/BENCH-STEP0.md — the rig regression recipe (expected lines, T1-T5 incl. a
  read(2)-across-the-seam check, evidence list) for the amix-kerntools rig.
- 9adfdcd: Z3660 rows (`-DZZ9K_TARGET_Z3660`: combo REJECT row first, then the fixed
  AGA-gated row with regw 4, reg_size/fb_offset 0x1000, fb_phys 0x200000, fb_max 0x2E00000,
  fw_major 1, console 1920x1080, NOCAPTURE); bench aids `-DZZ9K_BENCH_Z3` (ZZ9000 product 4
  inside the identity map) and `-DZZ9K_BENCH_Z3660` (drops the combo REJECT row because
  amiberry's piscsi model AutoConfigures as 0x144B0001). Tool reads the Z3660 FW lanes.
- Host check (cross gcc 2.7.2.3, stock 2 KiB headers, upstream flags, `-Wall`): every row
  selection compiles with exactly upstream's four warnings; `nm` undefined-symbol set and the
  nine entry points identical to upstream 75e2449. The scratch layout: copies of the driver
  dir beside symlinks to kerntools' `work/route-a-carrier-check/sysroot-usr/sys/{sys→include/sys,
  amiga/inc,amiga/console}` (a symlinked driver dir breaks `../../console` includes — copy it).
  GNU GCC 1.40.5 on the golden remains the real gate (bench step 0).
- Not done here, by design: any kernel build, any rig or metal driving.
- Self-review of the diff found two defects, fixed in the next commit: with `-DZZ9K_BENCH_Z3`
  the upstream product-4 REJECT row preceded the bench row (never reached) — the define now
  replaces it; SETREG on 32-bit rows did a lane read-modify-write, which cannot compose on the
  Z3660's write-only registers (read 0xFFFFFFFF) — now HI-lane latch + LO-lane 32-bit write.
- Rule kept: the probe on metal must never read 0x10000000 when the combo has AutoConfigured
  (autoconfig_rtg YES) — the REJECT row stays ahead of the fixed row; only a bench build
  (`-DZZ9K_BENCH_Z3660`, amiberry's piscsi model) drops it.
- e05d4da: zz9k_test accepts 1920x1080 and 1280x800 (needed to request any Z3660-valid mode
  other than 800x600/1280x1024/1024x768/640x480 from the tool).
- docs/BENCH-STEP2-STANDIN.md: the contract for the amiberry Z3660-RTG stand-in (placement,
  window, the 0x100 32-bit register file with per-register read/write behaviour, display
  rule incl. the X2==0 stride convention, chipset gate: the rig must run `chipset=aga` —
  amiberry's VPOSR id is 0x23 for AGA, 0x20 for ECS PAL (`custom.cpp:2413-2425`) and
  `cs_agnusrev` is not a .uae key) and the seven pass criteria for bench step 2.

## 2026-09-16 — bench step 0 PASS; step 1 recipe

- kerntools ran docs/BENCH-STEP0.md: PASS on every criterion (kernel 20408; kerntools 7dd7e21
  makes the record's `git=` load-bearing — the subtree is exported at the named commit, so
  this checkout no longer matters to their builds). Verified here against the evidence
  (`2026-09-16-zz9000-rig-step0/`): T1 transcript diff = the allowed lines only; T4
  values exact; T1/T2 screenshots byte-identical to the 2026-09-15 lane; -Wall identical (5,
  incl. the stock header's "/* within comment" — recipe corrected, c87420a). T3 prose fixed.
- docs/BENCH-STEP1.md: the -DZZ9K_BENCH_Z3 row on amiberry's ZZ9000 Zorro III model under
  `z3mapping=uae` (bases from 0x10000000; the piscsi model moves too → its direct-map arm);
  driver bytes pinned at e05d4da; T1-T5 as step 0 plus the capped `board_size` 0x2e10000 and
  a T6 negative test (`z3mapping=real` → "outside the identity map" + ENXIO).
- Bench step 1 PASS (kerntools, kernel 40227, `2026-09-16-zz9000-rig-step1/`): bench
  row at 0x10000000 size 0x2E10000; piscsi model at 0x18000000 booted via its direct-map arm;
  T2-T6 as specified (T6: "outside the identity map" + ENXIO). Finding, verified in
  `23-mmap-size-probe.txt`: `mmap(board_size)` fails ENOMEM under the login shell's default
  RLIMIT_VMEM (16 MB soft, hard unlimited) — ≥16 MB fail, ≤12 MB succeed — and succeeds for
  47 MB under `ulimit -v 131072`; T1 re-run that way is as specified, bars pixel-identical.
  Not a driver bug; a runtime contract for anything mapping board_size (Xzz9000 on the
  Z3660 row = 46 MB). Recorded: DESIGN.md §6/§8/Q11, BENCH-STEP1 (PASS, T1 under ulimit),
  BENCH-STEP2-STANDIN (criterion 0), and zz9k_test now restores the display and prints the
  ulimit hint when mmap fails (it used to exit leaving the RTG screen up). Kernel printf
  prints upper-case hex — expected lines adjusted.
- Fact for the metal plan (2026-09-16): the RGB capture sees only the
  Amiga RGB output; the Z3660 RTG HDMI is behind a hand-flipped HDMI switch → the metal recipe
  runs in console-side / RTG-side blocks with a flip between, and every RTG-side check has a
  serial/telnet-visible confirmation (transcripts, framebuffer readback, X log). Recorded in
  DESIGN.md §6 step 4 and Q6; A2 closed in docs/FIRMWARE-RTG-INTERFACE.md.
- Step-2 stand-in built in amiberry (amix-v830 814d25c, docs/z3660-rtg.md; pan bug fixed in
  71e7527). Rulings on its five flagged deviations recorded in BENCH-STEP2-STANDIN.md §4a:
  (a) CONFIG 0x104 is write-only in the firmware (no read case, rtg.c:772-1027) → reads
  0xFFFFFFFF, my table corrected; (b) key gfxcard_type=Z3660_RTG + gfxcard_size=128 accepted;
  (c) 0x2000-0x1FFFFF backed+logged accepted; (d) id 7 = 1920x1080, ids 12-15 mapped accepted;
  (e) FILLRECT/COPYRECT only accepted. Exact rig config and driver bytes (0fe840d) pinned.
- CHANGELOG.md was written on disk post-review; left untouched.
- Bench step 2 PASS (kerntools, kernel 40217, bytes c5a5a46, amiberry stand-in 814d25c + pan
  fix 71e7527; `2026-09-16-zz9000-rig-step2/`): boot line exact; 1920x1080 and
  1280x800 bars with NO pitch workaround; 1920x800 EINVAL; console at 1920x1080; register
  page readable via read(2) (`0000 0103` at 0x1a0); access log: nothing below 0x100 or in the
  test area; zzpanfix inert. Verified here. Rig: chipset=aga + chipset_compatible=A3000.
  Ruling: read() past board_size returns ENXIO — upstream's bound, kept; recipes reworded.
  DESIGN.md §6 step 2 PASS, §8 amiberry row → fixed. The Z3660 row is bench-proven end to
  end; lane parked until after 2026-09-19 (metal: HDMI switch blocks, A1/A6 reads, RLIMIT_VMEM
  in the X startup, the 040/060-port relink question).
- Rule (2026-09-16): RTG goes onto the demo machine only after the demo cards are fully
  ready. Recorded in DESIGN.md §6 step 4. Lane stays bench-proven and parked.

## 2026-09-16 night — "RTG for the demo" package (bench first; then the metal opened)

- The lane continued; the A4000D (demo card, rewritten the next day) then opened
  for a userland /dev/mem probe with the Z3660 firmware side operating — no kernel, no firmware, no
  card-file changes.
- The 68040/68060 port (A7 answered, measured in the r13 binary): cdevsw row 49 free at 0xa7c8; init
  via a second parinit wrapper (r13's parinit is already the z3660 wrapper); symbols all present
  EXCEPT builtinfont; conputc is a D pointer; scrcon_put override = the path to a VT shell on
  RTG; rlimits VMEM 16 MiB soft; 060 hazard: gcc -O constant division → 64-bit mulsl. Estimate:
  a day for the relink pass; metal validation separate.
- Driver changes tonight (all compile-checked; scan060 CLEAN, page geometry OK at -m68040 with
  the port's Model-B sysroot): ZZ9KIOC_CONPUTS + zz9k_cat (0fee1bc); tools zz9k_demo,
  zz9k_reg, ZZ9K_OPTS + console 1280x800 option (f5799f9); zz9k_memprobe (dc6565b); 060-safe
  console test, -DZZ9K_NO_BUILTINFONT, Z3660 device space = framebuffer alone because the
  register page holds the eth doorbells, zz9k_test raises RLIMIT_VMEM itself (6137966); one
  message per boot-path miss (389055d). The old object had two mulsl #1717986919 (the /10) —
  confirmed with scan060 before the fix.
- Docs: DESIGN §11 (the 060 assessment), BENCH-STEP3 (robustness, running at kerntools on
  40217), BENCH-DEMO (fresh kernel with console=, pinned 6137966), METAL-PROBE-RUN (the run
  sheet), DEMO-RTG.md (the one-pager; metal section pending).
- METAL PROBE PASS (Z3660 firmware side operating the A4000D, real LC060, r13, preset 7; evidence
  `2026-09-16-z3660-rtg-metal/`): the mmap variant refused its own self-test (user
  mmap of /dev/mem on r13 does not land where asked — recorded in DESIGN §11.5); the -r
  variant (kernel-executed lseek+read/write) ran end to end at 1920x1080 and 1280x800:
  FW_VERSION 0x0103 at 0x100001a0, framebuffer self-test MATCH, mode-set acknowledged by the
  firmware serial (video_mode_init 5 / 17), f800f800 readback, bars/blue/bars on the HDMI.
  Verified here from transcripts, serial and frames (f01 = the bars at 1920x1080; 00-before =
  the firmware splash "Z3 RTG AUTOCONFIG Disabled"). A1/A6/Q6 closed on metal.
- BENCH-DEMO PASS (kerntools, fresh kernel 56324, driver 389055d, console=zz9000consolewrite;
  `2026-09-16-zz9000-rig-demo/`): D1 boot line exact + `T zz9000consolewrite`; D2a
  zz9k_cat/CONPUTS renders uname/date/ls on the RTG at 1920x1080 (verified frame); D2b the
  c1.c hook's first on-box run (redirect enables, renderer draws; typed VT needs a keyboard);
  D3 zz9k_demo bars + fill/copy, ulimit inside the script. STEP3 R5 closed on zz9k_rw: reads
  and writes symmetric — crossing transfers cut to the window, ENXIO at/after 0x2E00000.
  The night's package is complete: DEMO-RTG.md final except the open decisions.
- The 68040/68060 port built the candidate kernel tonight (local only): unix-060-fpe-r13-zz9000-CARD1
  (sum 11366, build id 68040-260915-55; driver from 5fa58ba; DELIVERY.md with the metal
  checklist; counters +0x231c vs r13). Its question "mmap or read/write?" answered: the metal
  bars were read/write; the mmap variant refused its self-test → the user-mapping cache-mode
  row is OPEN. Added tools/zz9k_maptest (checklist row 1) and `zz9k_test rw` (bars via write(2))
  so no demo picture depends on the mapping (d85c037). DEMO-RTG.md updated.
- The 68040/68060 port identified the metal mmap mismatch: the port's ISSUE-46 (`mmmmap` rounds the PFN
  up → every /dev/mem mapping one 4 KiB page high; 2f004eb9 = .text+0x1000), not in the r13
  lineage or the candidate; /dev/zz9000 does not use mmmmap → tonight's evidence is NEUTRAL on
  the user-mapping row; zz9k_maptest stays row 1; /dev/mem mmap is not an oracle on r13
  kernels; leave r13's bytes alone for the demo. Recorded in DEMO-RTG.md and DESIGN §11.5.
- 2026-09-17: the candidate 060 kernel (r13 + driver 389055d) PASSED kerntools' emulator smoke
  (`2026-09-17-060-rtg-smoke/`): attach line first at io_init, multiuser, mkdev 49,
  zz9k_maptest PASS (mapping coherent in emulation), rw bars, mmap bars (the explicit question:
  YES on this kernel), zz9k_cat text with the built-in glyphs, bounds symmetric (ENXIO at
  0x2E00000 both ways), counters +0x231c. Doc nit for the port: DELIVERY §5 says EINVAL past
  FB_MAX; the driver returns ENXIO. The control (r13 on card 1) needed a card-0 restamp to
  boot in the emulator — a rig topology matter, not the driver's.

## 2026-09-18 — the X lane (Xzz9000 on the RTG, cross route)

- Groundwork (no design decision): the X11R6.3 tree prepared by the repo's own install.sh
  (sha-verified tarballs + overlay cb61a21 = upstream head) at 2026-09-18-xzz9000-cross/;
  host imake bootstrapped with the Amix BOOTSTRAPCFLAGS; a host-cpp wrapper (-undef, the
  compiled-in -Dlinux dropped) makes imake generate the on-box Amix Makefile byte-for-byte in
  its key variables. The libsocket link gap (toolchain "known, open") retired for the server:
  `-usyslog -useteuid` + usr/ccs/lib/libc.a after the shared libs. The sysroot lacks
  /usr/amiga (libscreen.so dangling, <amiga/screen.h> absent) — asked kerntools to pull it off
  the rig. Recipe and design in docs/X-CROSS.md; the build waited for a design review.
- Xzz9000 CROSS-BUILT (2026-09-18): 2026-09-18-xzz9000-cross/out/Xzz9000, 1 919 891 B,
  sha 709b8a97f5c0c073…; gates pass (ELF m68k, libc.so.1 interpreter, NEEDED by name, DDX
  entry points, 124 user-mode scan060 hits = ISP-emulated). Walls met and passed: /usr/amiga
  (kerntools pull), mkg3states (host-built generator), /usr/ucblib libdbm.a (pull; dbminit
  binds to libnsl.so anyway), the wrapper forwarding -ansi to ld (StandardDefines), four more
  libc.a-only symbols (stub archive now 12 members). No isoriano question needed. Recipe in
  docs/X-CROSS.md; bench recipe docs/BENCH-X.md; startx-rtg in x11/.
- METAL, first boot of the RTG card (Z3660 firmware side, r13+zz9000 kernel): the driver REFUSED the
  board — `zz9000: Z3660 combo board at 0x10000000 size 0x8000000 unsupported`. My bug: the
  REJECT row assumed an AutoConfig entry for 0x144B0001 implies autoconfig_rtg YES (board at
  0x40000000); on metal the hard-mapped combo still enters the board list (SCSI boot
  advertisement), at 0x10000000 with the 128 MB window. The discriminator is the BASE, not the
  entry. Fix: the autocon row carries the RTG window and attaches when reachable; the
  identity-map check refuses it with the advice otherwise; a refused/unanswering row continues
  to the next row (siblings' multi-method shape) so -DZZ9K_BENCH_Z3660 is no longer needed.
  Gates re-run: all four row selections compile clean; at -m68040 with the port's Model-B
  sysroot scan060 CLEAN, page geometry OK, U set unchanged. Needs a kernel rebuild.
- Candidate 2 built by the 68040/68060 port from 7e5c0e8 (68060-260916-51; counters +0x18 vs
  candidate 1 — its own status-facts table applies). DEMO-RTG carries the session-1 failure,
  the fix, the session-2 expected boot lines, the unchanged row order (startx-rtg last) and
  the honest limit: the bench exercises the fall-through, only metal exercises attach-by-
  autocon. All three bench recipes now state the three expected rig boot lines so the extra
  two are not read as a regression.
- Session-1 diagnostics (Z3660 firmware side, verbatim): TWO 0x144b autocon entries — idx1
  144b/03 type d1 at 0x00e90000 (64 KB, the Z2 combo) and idx2 144b/01 type 93 flags 70 at
  0x10000000 size 0x08000000 (the one my row matched and wrongly rejected). Diagnosis
  confirmed; base inside the identity map → driver fix, no firmware change.
- The `dd if=/dev/mem skip=…` hang is MY command, not a kernel finding: this box's dd
  read-skips character devices (our own step-2 evidence shows "read error during skip"), so
  skip=67108864 asks for 67M 4-byte reads. zz9k_memprobe lseeks, which is why it worked.
  Added `zz9k_memprobe -q` (signature only, no mode, no writes) as the canary for "does the
  board answer", for use only if an attach line is red. Recorded in DEMO-RTG.
- r2 shipped with /.xinitrc REPLACED by the bench session, which cost the card's NATIVE
  desktop its Workspace manager (startx and startx-rtg shared that file — my launcher's
  default was the cause). Fixed: startx-rtg now runs the first readable of $XINITRC,
  ~/.xinitrc-rtg, ~/.xinitrc, /.xinitrc — the RTG file first, so the machine's session file
  is never edited for the RTG; x11/xinitrc-rtg is the demo session (olwm + olwsm + xclock, no
  sleep needed with the two-probe launcher); the no-session-file fallback also starts olwsm
  when present. X-INSTALL-RTG §0/§2 updated with the shipped-image deviation and the
  next-rebuild list. olwsm on Xzz9000 is unproven — one bench pass after metal session 2.
- METAL session 2, row 0 PASS (Z3660 firmware side): `zz9000: autocon match pc 0x144B0001 base
  0x10000000 size 0x8000000` + `zz9000: Z3660 RTG at 0x10000000 size 0x2E00000 hw 0xFFFF fw
  0x103`, banner 68060-260916-51, multiuser, /dev/zz9000 present. The attach-by-autocon path
  — the one the bench cannot exercise — is validated; the board-table correction is proven
  where it failed. Rows 1-7 running.
- Metal session 2 rows 1-4: PASS (maptest coherent — the last open metal question answered; rw
  bars 1920x1080@60; zz9k_cat text; the demo's three pages). One deviation, mine: zz9k_demo and
  zz9k_console hardcoded upstream's /usr/amiga/bin/zz9k_test while the card installs to
  /opt/amix/bin. Fixed: both scripts now find zz9k_test via $ZZ9K_TEST, their own directory,
  then either conventional dir (ca430e8). Next-rebuild list consolidated in X-INSTALL-RTG.
- Metal session 2 row 7 PASS: the full OpenLook desktop (7 clients, decorated xterms, olwm
  menus) on the Z3660 HDMI at 1024x768 — rows 0-7 all green. Launcher finding, mine and a good
  one: EVERY readiness probe costs a server reset (last client out → serverGeneration++ →
  ZZ9000InitHW re-run, slow on the LC060), so my two-probe design left the server
  re-initialising when the session's clients arrived ("Can't Open display"); RTG_SETTLE_AFTER=25
  was the metal run's runtime fix. Launcher now probes ONCE and then waits for the server log to
  go quiet (3 stable seconds, bounded by RTG_SETTLE_MAX=90) — adaptive instead of a guessed
  number — plus a 5 s floor. sha 26234982; supersedes 0f59bbe3 in the next-rebuild list.

## 2026-09-26 — status of record (docs sweep)

- METAL-PROVEN end to end, initial working version: candidate-2 kernel (7e5c0e8) + cross-built
  Xzz9000 → OpenLook desktop on the Z3660 HDMI from a real A4000 (the A4000D with the card it then
  held; session 2 rows 0-7 above; status of record 2026-09-19).
- Proven artefacts archived 2026-09-26 (outside the repo):
  unix-060-fpe-r13-zz9000-c2-CARD1-ced0s1-metal-green-2026-09-16, the CARD0 restamp
  unix-060-fpe-r13-zz9000-c2-CARD0-c6d0s1-2026-09-16, Xzz9000-z3660rtg-metal-green-2026-09-16,
  zz9k-tools-z3660rtg-6358112-metal-green-2026-09-16.tar.
- Performance is poor and is the next lane. Whether ZZ9000 and Z3660 stay one driver is still
  open. The card in use is now card 2 in the A4000T.
- Standing: /dev/mem mmap broken on r13 lineage (ISSUE-46), not ours. Bench = amiberry's ZZ9000
  emulation in the kerntools rig. X build inputs host.def + imakecpp.sh are tracked in x11/cross/
  (scrubbed copies).

## 2026-09-27 — P-5 step 0: measurement kit (RT-01) and the first rig result

- P-5 started. RT-01: x11/bench/ (4aa1edd). It holds x11perf from the R6.3 tree, Antti's xbench
  unchanged, and two discriminators (xorfill = S1, copyexpose = S2). Everything links a static
  R6.3 Xlib. build.sh reproduces the binaries byte for byte from a fresh copy of the Xzz9000
  work dir.
- Rig smoke, LC060 + Z3660_RTG stand-in, a work copy of the X-ready desktop image, current
  startx-rtg at 1024x768 with the olwm session. Evidence is in `2026-09-27-rt01-rig/`.
  - x11perf `-rect10` gave 23100/s.
  - xbench noop gave 70588 ops/s.
  - The first xbench died with Illegal Instruction: `-m68040` emits 040-only FPU opcodes that the
    FPU-less LC060's emulation traps on. The clients are now built with plain `-O`.
- **S1 CONFIRMED on the rig.** xorfill with two GXxor passes leaves `fill` FAIL (10000 px) and
  `wide` FAIL (1200 px); the zero-width `thin` control PASSES. That is ZZ9000SolidRect ignoring
  the alu. Frames: 20-xorfill-once.png (reference), 21-xorfill-twice.png.
- **S2 CONFIRMED on the rig.** copyexpose shows `exposures 0` in both cases, and the destination
  reads back the covering window's red where its blue background belongs (win->win); the
  self-scroll does the same. The DDX CopyArea copies obscured screen pixels and sends no
  GraphicsExpose. Frame: 30-copyexpose.png.
- On that rig session the root window showed white, not xinitrc-rtg's #4a5a6a. This is
  unexplained so far, so it is logged here but not yet a finding.

## 2026-09-27 — P-5 S3: the framebuffer mapping's cache class (input from the va2000 analysis)

- Source: a project analysis of Antti's va2000-amix wip/zorro3
  (`2026-09-26-collab-zorro3/va2000-wip-zorro3.md`). On the 040/060 line, a driver
  registers its framebuffer PFN range with `hat_cm_fb_add(lo, hi)` (the base kernel's
  `hat_cm_fb` table in `hat040.s`: 2 slots, immutable once installed). User mappings of device
  pages inside that range get NC (0x60, noncacheable, not serialised); all other device pages get
  NCS (0x40, noncacheable, serialised). VA2000 registers at attach; its user framebuffer is NC.
- Our driver never calls `hat_cm_fb_add` (grep over overlay/), so Xzz9000's framebuffer mapping
  is NCS on that line. That rules out the correctness half of S3, the copy-back stale-pixel
  theory for the artefacts; S1/S2 fully account for the observed artefacts anyway.
- What remains of S3 is performance only: NCS vs NC for CPU stores across Zorro III, one knob.
  It belongs with fix 2/5, not fix 1. Before proposing it, read va2000.c on that branch
  (registers via `dev_kvmap` NCS, `hat_cm_fb_add` at attach).
- Caveat to verify first: the analysis cites the port's `hat040.s` (the fresh relink base), while
  DEMO-RTG recorded the per-map cache mode as a deferred TODO on the r13 / candidate-2 lineage.
  Confirm that candidate-2's base carries the `hat_cm_fb` table and the NCS default before
  relying on either statement for the metal kernel.
- Not relevant to upstream's X server: this is kernel-side and port-specific, and the X
  server patches do not touch hardware access.

## 2026-09-27 — P-5 fix 1 accepted on the rig: before/after (709b8a97 vs s5)

The rig is the same as the smoke: LC060 + Z3660_RTG stand-in, a work copy of
the X-ready desktop image, 1024x768, olwm session, and `xset s off` after every server start.
Evidence is in `2026-09-27-p5-rig/`. Servers: 709b8a97 = the metal server; s5 =
Xzz9000 with p5-1..p5-4 (6c726318).

| client | 709b8a97 | s5 |
|---|---|---|
| xorfill fill / wide / thin | FAIL 10000 / FAIL 1200 / PASS | PASS / PASS / PASS |
| copyexpose win->win | FAIL (0 exposures, RED) | PASS (1 exposure, area 20000, blue) |
| copyexpose scroll | FAIL (0 exposures, RED) | PASS (1 exposure, area 10000, green) |
| xorcopy image / pixmap / window | FAIL 10000 ×3 | PASS ×3 |

**Acceptance met:** every client passes on s5 and fails as predicted on 709b8a97.
- s1, s2 and s4 were not run: s5 carries all four, and the per-fix split is in the patches.
- The base copyexpose frame here is black because of the screen saver. Use RT-01's
  `30-copyexpose.png`, the same server on the same rig.

**x11perf pair** (Antti's list, 3 s × 3 reps; committed as `x11/bench/perf/p5-rig-709b8a97.txt`
and `p5-rig-s5-6c726318.txt`; emulator numbers, for A/B only). Most tests lie within their spread.
Where the delta exceeds the spread:
- Scroll 10x10 is 24% slower (11433 → 8673/s) and window→window Copy 10x10 29% slower
  (11600 → 8257/s), with spreads ≤ 1.4% on the base. That is about 35 µs more per small copy:
  S2's region work plus `miHandleExposures` on every window→window CopyArea. At 100x100 and
  500x500 the cost is lost in the pixel time (−2.5% and −7.5%; the latter is within spread).
  **Candidate for fix 2:** skip the region clip and `miHandleExposures` when the source rectangle
  lies wholly inside a one-box clipList (nothing obscured; a NoExpose is still owed when
  graphics exposures are on).
- PutImage 10x10 is −4.0%, PutImage 100x100 −2.6%, GetImage 100x100 −2.2%, pixmap→pixmap Copy
  −0.9%, and image text TR10 −2.8%: small deltas just outside tight spreads. PutImage now takes
  the per-slice alu test in SetSpans (S4).
- Everything else (fills, lines, circles, text, large copies) is within spread: no change, as
  expected for correctness fixes.

**Rig lessons:**
- The server's default screen saver (600 s, blanking) blacks the frame, and it starves
  olwm-managed windows of their first Expose, so xorfill/xorcopy hang under it. `xset s off`
  now follows every server start (to go into xinitrc-rtg).
- Amix `grep` has no `-E`; use `egrep`.
- amixrun gives up on commands over ~20 s; long guest commands must be `nohup`'d.
- Guest time runs faster than wall time here, so client holds must be long (120 s) for a frame.
- xorcopy's olwm-placed target once overlapped its source window (FAIL 165 px on s5, a restack
  between the two copies). Both windows are now override-redirect at fixed places: 709b8a97
  FAIL ×3, s5 PASS ×3.

**olwm opaque move (the metal run's open item), answered on the rig:**
- Amix's olwm (1991, USL/Xol family) knows only the resource `moveOpaque` (strings; no
  `DragWindow`), so `-xrm 'OpenWindows.DragWindow: True'` was ignored.
- With `-xrm '*moveOpaque: True'`, xdrag moves the xterm 300,0 (frame `olwm-opaque.png`).
- The outline control under a freshly started olwm also gives 300,0.
- XTEST drives both modes, and the server is not at fault.

## 2026-09-27 — post-A/B batch

- `x11/xinitrc-rtg` now runs `xset s off` after olwm has settled. It does not
  go at the very top: a short-lived first client makes the server reset under the window
  manager. The box's R5 `xset` has no DPMS option, so blanking is the only thing turned off.
  RUN.md keeps the per-run `xset s off`.
- RUN.md corrects what `-text` does: x11perf's fallback is a substring match over option names,
  so it runs all sixteen `*text*` tests, not `-gc`.
- The cache class of the zz9000 user framebuffer is NCS on r13 / candidate 2 / candidate 3 (68040/68060
  port, `HAT-CM-FB-R13.md`). DEMO-RTG and DESIGN §11.5 are corrected; DESIGN §3.1's
  autoconfig-YES cell is now "unverified" rather than "copy-back via hat_cm_ram".

## 2026-09-28 — rig: fix 2a and fix 2 measured, p5-7, RT-06

Rig: same config as before; evidence in `2026-09-28-p5-fix2-rig/`. The x11perf files
are committed in x11/bench/perf/ (s6, s7, and the s8rt04 subset). Emulator numbers are for
A/B only.

- **Correctness:** s6, s7 and s8rt04 all PASS on the rig (xorfill ×5, copyexpose ×2, xorcopy ×3).
- **Fix 2a (s5 → s6):** Scroll 10x10 +26.8 % and window→window Copy 10x10 +30.8 %, almost back to
  709b8a97 (S2's cost recovered). The s6 run's -dot (−66 %) overlapped the session's start-up
  (the first test ran right after startx); it is not a fix-2a effect, and the s8 subset shows
  -dot back within spread.
- **Fix 2 (s6 → s7):** long runs gain.

  | test | gain |
  |---|---|
  | rect100 | ×2.55 |
  | rect500 | ×2.74 |
  | scroll100 | ×1.85 |
  | scroll500 | ×2.18 |
  | window/pixmap copies | +18 to +33 % |
  | PutImage100 | +11 % |

  Short runs lost: -dot −17 % (vs s5), -rect1 −12 %, -line10/-line100 −7 to −10 %.
- **p5-7 (f2e0cd1, s8):** runs under 4 pixels keep the 16-bit loop. -rect1 +5.4 % and -line10
  +4.5 % vs s7; -rect100 and -scroll100 unchanged. Against s5 the tiny ops are now within their
  (wide) spreads. An inline call-site variant (s9) measured no further gain and was not kept.
- **Metal, MG6 (Z3660 firmware side):** the undecorated xclock appeared on both s6rt04 and s7rt04, so it
  is not fix 2. olwm needs 50–110 s to come up on the LC060. s7rt04 is accepted for correctness
  on metal.
- **RT-06 (51e0864):** xinitrc-rtg waits for olwm's OL_MANAGER_STATE bit (the box's olwm reads
  0x3 with olwsm) instead of a fixed 5 s, bounded by WMWAIT = 120 s. Rig smoke: xclock decorated.
- **The hooked server on a kernel without monsw (s8rt04, rig):**
  - startup OK;
  - two forced resets (InitHW count 3 → 5) and the server survives;
  - idle-session exit is clean ("session exited (status 0); stopping the server");
  - the ioctl's EINVAL is ignored as designed.
- **Publication note:** x11perf's banner prints the node name; the committed perf files have
  it stripped (xrtg-amix's rule for public perf files).

## RT-05 (NC framebuffer class): measured on metal, no gain, not shipped

MG8, A4000T, 2026-09-28/29. Kernel A = KT-12f; kernel B = the same with `-DZZ9K_FB_NC` (B's boot
line confirmed the registration). Both ran s8rt04. Every x11perf row was within ±1.7 %, and the
store-bound set overlapped fully:

| test | A | B |
|---|---|---|
| rect100 | 591 | 592 |
| rect500 | 25.2 | 24.9 |
| scroll100 | 314 | 314 |
| copywinwin100 | 207 | 206 |
| putimage100 | 68.9 | 69.5 |

No read-back failure. The table is in `2026-09-28-mg/mg8/perf-A-vs-B.txt`.

Reading: the Zorro III write cycle bounds the store path, not the 060's serialisation. NC is
declined. 26ff2d0 stays in the source behind `-DZZ9K_FB_NC`; with the flag off the objects are
byte-identical, so no relink pass is needed. The remaining lever is fix 3: let the card's own
engine do the fills and copies.

## RT-07 (P-5 fix 3): fills and copies on the card's blitter

Dispatched 2026-09-28. The firmware facts were checked against the Z3660 fork before any code:
- FILLRECT (0x134) and COPYRECT (0x138) run synchronously, and the triggering write is
  acknowledged only when the operation is done.
- `BLIT_DST`/`BLIT_SRC` are byte offsets from `FRAMEBUFFER_ADDRESS` (`RTG_BASE+0x200000`), which
  is our `fb_phys`; so 0 is pixel (0,0), as in the P96 driver's `r->Memory - MemoryBase`.
- `ROW_PITCH` counts 32-bit words. X2/Y2 are the width and height.
- A 16-bit fill takes the low half of RGB *after* the firmware byte-reverses it (rtg.c:1214
  `rect_rgb = swap32(zdata)`, where zdata is already the value as written). So the 68k must
  write a 16-bit colour in the HIGH half. The first RT-07 driver wrote it in the low half, and
  every fill came out as 0; corrected on the rig on 2026-09-29, see below.
- `copy_rect_nomask` orders rows by `sy < y1` and uses memmove when `sx < x1`, so overlap in
  either direction is handled in the firmware. The one case it leaves to a forward `memcpy_neon`
  is a same-row move to the left (dst < src), which is safe for a forward copy. Metal
  blit-edge covers it.

**Driver** (`zz9000.c`/`.h`):
- New row flag `ZZ9K_BT_BLITTER` (0x0008), set on both Z3660 rows. `-DZZ9K_NO_BLITTER` drops
  it; `-DZZ9K_BENCH_BLIT` gives it to the ZZ9000 rows for amiberry's ZZ9000 model.
- The dormant upstream `zz9k_blit_fill`/`zz9k_blit_copy` now run when the flag is set, the
  mode is 16-bit and the row is a whole number of longs; otherwise the CPU loops run as before.
  FILLRECT/COPYRECT go through same-signature wrappers, `zz9k_fillrect`/`zz9k_copyrect`.
- The first version of those cases grew the ioctl switch, and its jump table then decoded as a
  scan060 `movepl` (the 407d379 trap again). The wrappers keep the case bodies, and the table,
  at their old size.
- `ZZ9KIOC_BLITS` (IOC|16) takes up to 1024 fill/copy records, copied in 8 at a time. Each
  record is checked against the screen just before it runs; a bad one gives EINVAL. It answers
  ENXIO when the blitter does not serve the board. It is handled before the switch.
- Kernel-patchable: `zz9k_blit_on` (1) and `zz9k_blit_maxarea` (0 = whole operation, else
  bands of at most that many pixels; a downward copy runs its bands bottom-up).
- Imports unchanged: the seven plain imports, plus monsw's three with `-DZZ9K_MONSW`. The gate
  emulation (`2026-09-28-rt07/gate.sh`) passed for all four objects: scan060 CLEAN,
  no COMMONs, no name collisions, 4 KiB page shifts.

**DDX** (`x11/patches/rt07-card-blitter.patch`, s10rt04):
- `-blitmin N` routes window fills and window-to-window copies of at least N pixels to
  `ZZ9KIOC_BLITS`. It works through the two existing funnels, `blitFill` and `copyVRAMBox`.
- The queue keeps drawing order: it is flushed before any CPU drawing and before each entry
  point returns.
- A refusal turns blitting off for the life of the server and redraws that batch on the CPU.
- Default 0 = never, so the patch is inert until metal picks a value.
- The chain from cb61a21 reproduces the build tree.

**Client** (`x11/bench/blit-edge.c`):
- 1050 cases read back against a client-side reference: every alignment, all four edges, eight
  overlap directions, clears, and child-window moves.
- On a host Xvfb at depth 16 all 1050 PASS. A one-pixel mutation fails 533.
- `-stress SECS` is the load for the metal hold-off cell.

**RT-07 rig (2026-09-29).** Kernel KD060-Z-RT07-c0-u6 (1dd9a89a, zz9000 object 40eebe2d),
checked on the vehicle. Root6 was a copy of the rt01 X disk, on the Z3660_RTG stand-in, at
1024x768. Evidence is in `2026-09-29-rt07-rig/`.
- **s10rt04 without -blitmin (the control):** blit-edge 1050 cases, 0 FAIL. xorfill 5/5,
  copyexpose 2/2, xorcopy 3/3.
- **s10rt04 -blitmin 1:** the log says "card blitter in use from 1 pixels".
  - Every copy PASSed.
  - **Every fill FAILed with 0x0000**: 177 fills, 143 clears, and 18 moves (the moves fail on
    the parent's background).
  - copyexpose win->win FAILed on its green fill. The other clients PASSed.
  - Cause: the RGB byte order above. Fixed in the driver (Z3660 rows write `color << 16`).
    It needs a relinked rig kernel before it can be re-run.
- The first control run was contaminated. xclock/olwsm mapped above blit-edge's window while
  the session was still starting. blit-edge now raises its window under the server grab. On
  Xvfb with a competing raiser, the old client fails 139/1050 and the new one PASSes 3/3.

**RT-07b rig (2026-09-29): PASS.** Kernel KD060-Z-RT07b-c0-u6 (d891e468, zz9000 729cbdc,
object 08500bbb), checked on the vehicle. The same root and stand-in as the first run. Evidence is
in `2026-09-29-rt07b-rig/`.
- **s10rt04 -blitmin 1:**
  - The log says "card blitter in use from 1 pixels".
  - blit-edge (9ec5ad87): 1050 cases, 0 FAIL.
  - xorfill 5/5, copyexpose 2/2 (the green fill is correct now), xorcopy 3/3.
- **olwm drags:** outline and opaque (`moveOpaque`), xdrag 300 px. Both moved (95,367 to
  395,367), with no trails in the frames.
- **Control frame:** a frame of the same session without -blitmin is pixel-identical to the
  blitter frame. The black root is the DDX's existing first-pixel approximation of the root
  tile, not the blitter.
- Phase A (the control) stands from the first run; the server is the same.
- The rig proves correctness only. It says nothing about speed or the interrupt hold-off; those
  are in the metal cell (`2026-09-28-rt07/METAL-CELL.md`, hashes refreshed for RT-07b).

## MG10 follow-ups and RT-08 (2026-09-29)

Rig evidence is in `2026-09-29-rt07c-rig/` (quiet rig, KD060-Z-RT07b).

**1f — the 7-pixel triangle (MG10).**
- The mark is a CPU-path difference, but not a drawing change. rtg's screen init never called
  `miSetZeroLineBias` (only `miScreenInit` does), so `miZeroLineScreenIndex` stayed 0 and
  `miZeroLine` read its Bresenham tie-break bias from `devPrivates[0]`, a heap pointer.
- Proof:
  - The pre-RT-07 source rebuilds byte-identical to s8rt04, so the builds are reproducible.
  - s8/s10/s11 draw olwm's window-menu triangle differently. Each stays stable across xrefresh and
    a 40 px drag, and gives a different `zline` checksum.
- Neither old rendering is "the" correct one: X leaves zero-width tie-breaks to the device. The
  defect was that ours changed per build.
- **Fix: d11c6e7 (s12rt04), which sets `DEFAULTZEROLINEBIAS`.** s12 and a heap-shifted s12 (a
  64 KiB data array added) give the same zline checksum (0x9cfb4dde) and the same triangle.
- Host Xvfb (modern fb) is not a reference: it sets 5121 pixels where R6.3's mi sets 5122–5123.
- Antti's R5 Xrtg has no zero-line bias, so there is nothing to port there.

**-rect1 −20 % (MG10).**
- Cause: each window fill made three calls, plus a flush call per request.
- **Fix: ecf5e7a (s11).** Macro tests, SolidRect's own loop below -blitmin, and an empty flush is
  one compare. Rig -rect1: s8 111000, s10 77500, s11 117000/s.

**RT-08 — banding by default. af226e6, s13rt04 = 10f3e9e4.**
- Why: MG10 part 3 measured one full-screen blitter op stalling the 060 for ~20 ms, with 42–44 %
  serial loss.
- Where the banding lives: in the server. The driver runs a batch record by record at base
  priority, so interrupts are taken between records, and no kernel change is needed.
- `-blitmin` now defaults to 1024 (MG10 kept the 100-size gains there). The new `-blitmax`
  defaults to 8192 px, about 0.16 ms at ~50,000 px/ms, under one 38400 character time.
- A refusal can now land mid-operation. The remaining rows are drawn on the CPU, never the whole
  operation again.
- Host `x11/bench/band-test.sh`: 6930 cases, 0 FAIL. The mutations (top-down bands, whole-op redraw)
  fail 1240 and 156.
- Rig, s13rt04:
  - At defaults: log "in use from 1024 pixels"; blit-edge 1050/0 and all three clients PASS.
  - `-blitmin 1 -blitmax 256`: blit-edge 1050/0.
  - With the kernel refusing (`zz9k_blit_on` 0 via adb on /dev/kmem, mid-op on the first
    full-screen paint): blit-edge 1050/0.
  - The desktops at defaults and when refused are pixel-identical to s8rt04's.
- Rig x11perf. The emulator's timing is noisy and its blitter is not the card's, so this is A/B
  only; MG11 decides:

  | test | s8rt04 | s13 banded (defaults) | s13 unbanded (`-blitmax 0`) |
  |---|---|---|---|
  | rect1 | 86800 | 87500 | 86700 |
  | rect10 | 29700 | 31100 | 31000 |
  | rect100 | 932 | 8460 | 10900 |
  | rect500 | 42.1 | 631 | 1430 |
  | scroll100 | 606 | 2670 | 3180 |
  | scroll500 | 35.4 | 459 | 1370 |
  | copywinwin100 | 465 | 2420 | 2700 |
  | copywinwin500 | 21.5 | 428 | 1150 |

- The kernel-side `zz9k_blit_maxarea` (patched live via adb) gave erratic rig numbers
  (maxarea 0 ran slower than 50000), which is further evidence that the rig cannot rank band
  sizes.
- **The 500-size cost of banding, and the fallback if MG11 disappoints.** A 500x500 op at 8192 px
  becomes 31–32 records of 16 rows. The rig shows −56..−66 % against unbanded, still ×15 to ×20
  over s8rt04; the 100-sizes (2 bands) lose −13..−22 %.
  - Most of the per-band cost is register writes over Zorro III. Each one is a firmware-handled
    write on the ARM, and every record re-sends the whole blitter set:
    - `zz9k_blit_fill`: DST, RGB, ROW_PITCH, COLORMODE, X1, Y1, X2, Y2, FILLRECT (9 writes);
    - `zz9k_blit_copy`: 11 writes.
  - Between the bands of one operation only the rows change. The firmware keeps every blitter
    register in globals across writes (rtg.c `rect_x1`…, `blitter_dst_offset`, `blitter_colormode`),
    so the driver could skip the unchanged ones:
    - keep the last written DST/SRC/RGB/ROW_PITCH/COLORMODE/X1/X2/X3 in the board struct and
      write only what differs;
    - a fill band then costs Y1, Y2 and FILLRECT (3 writes), and a copy band Y1, Y2, Y3 and
      COPYRECT (4).
  - That is about a third of the writes per band. It is a driver change: a relink, then a rig
    blit-edge run, then metal.
  - It holds only while this driver is the only writer of those registers. The ethernet driver
    uses other registers, and the kernel console draws with the CPU. If either changes, the cache
    has to be invalidated or dropped. The cache must also be invalidated at mode set.
  - The other levers are a larger `-blitmax`, if serial input at 38400 does not matter (MG11 retries
    4096 only when 38400 still loses bytes), or raising `-blitmin`. Neither needs a relink.
- The metal cell is `2026-09-28-rt07/MG11-CELL.md`.

## RT-10: the console keyboard after X (2026-09-29)

MG11's HID check on the A4000T found that after an RTG X session, typing on the console no longer
reached the console shell. Telnet was fine.

**Cause (kernel source, console/screen.c and scrdev.c):**
- `rtgOpenInput` selects the server's screen for input with `SIOCACTIVATE` and never displays it.
- When the screen closes, `CloseScreen` looks for a successor only in the screen's own group. The
  server's screen has no group (`group=-1`), so it finds none, and it sets `activescreen = 0`.
- The keyboard handler returns early when there is no active screen, so every key is dropped.
- This is not `-kb`, which in R6.3 means "disable XKB", and not the monitor switch. Every rtg
  server has it: s8rt04 does too.

**Fix: s14rt04 = 4f2c90dc (rt10-console-keyboard.patch).** Before the input screen closes, the
server issues `SIOCBACK` (`HideScreen`) on it, in `rtgCloseScreen` and in `rtgAbort` (the
FatalError path). `HideScreen` acts only while our screen is still the active one, and then
selects the first usable other screen: the console.

**Rig, one boot, quiet, KD060-Z-RT07b** (evidence `2026-09-29-rt10-rig/`).
Console typing through amiberry's keyboard, checked by reading the file over telnet:

| after | typing |
|---|---|
| before X | works |
| s8rt04 | LOST |
| s14rt04 | works (and recovers the loss) |
| s14rt04 | works |
| s13rt04 | LOST |
| s14rt04 | works |
| s14rt04 killed with -9 | LOST |

A `kill -9` of the server still loses the keyboard, since SIGKILL runs no server code. A
segfault or other fatal signal goes through FatalError, so rtgAbort covers it. Any later RTG X
session hands the keyboard back.

If `kill -9` needs covering, a small box tool could do it, called from startx-rtg's exit trap:
OpenScreen, then `SIOCACTIVATE`, then `SIOCBACK`, then close.

## RT-11: the keyboard after a killed server (2026-09-29)

`x11/rtg-kbdback.c`: OpenScreen, then `SIOCACTIVATE`, then `SIOCBACK`, then CloseScreen. Built at
4259 B, sha256 f636e9df; scan060 CLEAN; its only imports are libscreen.so and libc.so.1.

startx-rtg runs it from the exit trap only when the server died on its own: the server is gone,
and this script did not stop it. Amix `sh` makes status-based detection impossible: `wait <pid>`
returned 0 for a `kill -9`, for a `kill -15` and even for `exit 3` (measured on the rig).

Rig, one boot, KD060-Z-RT07b, s14rt04 (`2026-09-29-rt11-rig/rt11-results.txt`):

| case | typing |
|---|---|
| before X | works |
| kill -9, run 1 | works |
| kill -9, run 2 | works |
| normal exit (the tool is not run) | works |
| kill -9 with the tool moved away (control) | LOST |
| a normal session afterwards | works |

## RT-09: the blitter register cache (2026-09-29)

**dabe447.** Per band, zz9000 sends only the registers that changed: a fill band is Y1/Y2/FILLRECT,
and a copy band is Y1/Y2/Y3/COPYRECT.
- MG11 measured ~0.14 ms per band for re-sending all 9–11 firmware-handled register writes.
- Fills and copies write COLORMODE with the same value, because FILLRECT ignores the mask byte.

**When the cache is emptied:** it lives for one ioctl. It is emptied at every blitting ioctl
(BLITS, FILLRECT, COPYRECT), at mode set and at attach.
- The firmware writes those globals only in their own register handlers (rtg.c:1073–1215).
- Between ioctls, anything may happen: SETREG, a mode set, another process. Within one ioctl
  nothing sleeps, and no interrupt handler writes these registers.

**Gate emulation:** scan060 CLEAN and imports unchanged, with objects monsw 40d3b16c and plain
2210dc32.
- The port's PO-13d (472546b) matched.
- KT-20d is KD060-Z-RT07c (b9cee294…6eda275, sum -r 08512).

**Rig, quiet, s14rt04** (evidence `2026-09-29-rt09-rig/`). On RT07c:
- Defaults: log "in use from 1024"; blit-edge 1050/0; xorfill 5/5, copyexpose 2/2, xorcopy 3/3.
- `-blitmin 1 -blitmax 256`: blit-edge 1050/0.
- Mode change to 1280x1024 (a new pitch, `-blitmin 1`): blit-edge 1050/0.
- The desktop is pixel-identical to RT07b's s13 and s8 desktops.

**x11perf, RT07c against RT07b, s14 at defaults.** The two agree within noise:

| test | RT07c | RT07b |
|---|---|---|
| rect100 | 8520 | 8470 |
| rect500 | 631 | 639 |
| scroll100 | 2590 | 2650 |
| scroll500 | 483 | 458 |
| copywinwin100 | 2340 | 2390 |
| copywinwin500 | 428 | 414 |

The emulator makes a register write almost free, so the rig cannot show the saving. MG12 on metal
measures it (`2026-09-28-rt07/MG12-CELL.md`).

## Where the per-band time goes (analysis after MG12, 2026-09-29; no code)

**The numbers, corrected for band shape.** At 1280x800 a band is whole rows: 8192 px is 6 rows
(134 bands per full-screen op), and 2048 px is **1 row** (800 bands, not 500).

| run | ms/op | extra over unbanded (20.42) | extra per band |
|---|---|---|---|
| MG11, RT07b, no cache, 8192 | 37.69 | 17.27 | 0.129 ms |
| MG12, RT07c, cache, 8192 | 36.42 | 16.00 | 0.119 ms |
| MG12, RT07c, cache, 2048 | 114.37 | 93.95 | 0.117 ms |

- **A fixed ~0.12 ms per record, independent of band size.** x11perf agrees: rect100 is 1, 2 and 5
  bands at unbanded/8192/2048, at 0.123, 0.229 and 0.461 ms per rect. That is 0.08–0.11 ms per
  extra band.
- **The register cache removed ~7 of ~10 register writes per band and saved 0.010 ms per band.**
  So one register write costs **~1.4 µs**, and ~0.11 ms per record comes from something else.

**What the firmware does per 68k bus access.** The rtg code is `rtg_thread`, one cooperative
protothread in the real-CPU main loop (main.c:1932–1965). Each iteration runs:
- `measures_thread` (IIC sensor reads, rtg.c:480–553);
- `interrupt_thread`;
- `rtg_thread`, which serves **at most one** bus access per turn (`PT_WAIT_UNTIL` on REG1,
  rtg.c:556; dispatch rtg.c:560–628; ack handshake rtg.c:716–745);
- then `reset_thread` every 40th turn, `ethernet_thread`, `usb_thread` and `other_tasks()`.

The dispatch and ack are short. The op itself runs inline in the write handler (FILLRECT
rtg.c:1239, COPYRECT rtg.c:1254), with `set_fb` plus a NEON row loop (gfx.c:121, 180–208). There
is no cache flush, delay or mailbox on the op path: the only `Xil_DCacheFlush` calls are in ARM_RUN
(rtg.c:1819/1822).

**Hypotheses, with what separates them:**
- **H1: the firmware's main-loop turn.** A trigger write is followed by the op, and while it runs
  the other protothreads wait. Afterwards the loop may run a long turn (an IIC transaction in
  `measures_thread`, ethernet/USB backlog, `other_tasks`) before `rtg_thread` sees the next write.
  - A plain register write costs only ~1.4 µs, so a long turn would have to be triggered by the
    op itself, e.g. by work that piled up while the op held core 0.
  - That fits the numbers: ~0.1 ms of deferred housekeeping per op.
- **H2: the ack handshake after a long op** (rtg.c:731: toggle ACK until REG1 clears).
  - Profiling is already in the source: `PROFILE_ACK_REQUEST` (rtg.c:717) prints any ack over
    100 µs.
- **H3: the 68k side per record.** The zz9000 BLITS loop, copyin per 8 records, the X server's
  queue. This is expected to be µs, not 0.1 ms; the emulator shows no per-band cost at all.
- **H4: an area-independent cost inside the op routine.** Nothing is visible in gfx.c.
  M1(a)/(c) settles it.

**Measurement plan (Z3660 firmware side, one boot, RT07c, no relink):**
- **M1:** a small userland bench, `zz9k_blitbench` (proposed; about 150 lines, cross-built). It opens
  /dev/zz9000, SETMODEs 1024x768/16, then times N=2000 records with gettimeofday for:
  - (a) identical 1x1 fills in one BLITS batch: with the cache, the trigger only, i.e. the fixed
    per-op cost;
  - (b) 1x1 fills alternating y: (b) − (a) is one register write;
  - (c) 8192-px fills (8x1024): ((c) − (a)) / 8191 is the per-pixel cost;
  - (d) (a) as 2000 single-record ioctls: (d) − (a) is the ioctl cost;
  - (e) 1x1 copies, as a check that copies match fills.
  - It prints µs per record for each.
- **M2 (firmware lane):** a test build with `PROFILE_ACK_REQUEST` enabled, run under M1(a). Slow
  acks point at H2. Also port the core-0 task stamps, which exist only in the emulator loop
  (C0_STAMP, main.c:1851–1893), to the real-CPU loop (main.c:1932–1965). They show which
  protothread holds core 0 after each op (H1).

**Reading:**
- If M1(a) is ~0.1 ms and M2 shows the time in another protothread, the fix is in the firmware:
  let `rtg_thread` serve consecutive bus accesses without yielding, or skip housekeeping while
  a blitter burst is running. That is a Z3660-lane item for 1.1.
- If M1(d) − (a) is large, the fix is on our side (fewer, larger batches).

**Current decision:** the 8192 default stays. MG12: correct; 9600 at idle; 38400 gaps 258 against
idle 22. 2048 is not worth 3x the time for 38400 alone.

**M1 tool: `x11/bench/zz9k_blitbench` (2bb6c0a4, 12913 B; built by build.sh).** It runs with no X on
the card; it sets 1024x768/16 itself. It prints µs per record for the cases (a)–(f) above:
- a = the fixed cost per op;
- b − a = one register write;
- c = an 8192-px fill;
- d − a = one ioctl;
- e and f = copies.

On the rig it runs (emulator numbers, meaningless for metal): a 26.4, b 32.0, c 31.0,
d 1107 µs (the emulated syscall), e 30.5, f 44.4.

Metal recipe (Z3660 firmware side, one boot, RT07c or RT07b, when the bench is idle; 1.1 preparation):
- no X running: `/opt/amix/bench/zz9k_blitbench -secs 3`;
- run it twice;
- keep both outputs.

**M2 brief (Z3660 lane, after the release): firmware profiling of the op path.**
1. **Ack profile.** Build with `#define PROFILE_ACK_REQUEST` (rtg.c:717).
   - It already times each bus access from `ack_request` to the REG1 release, and prints any over
     100 µs.
   - Add the register offset (`zaddr`) of the access to the print, so a FILLRECT/COPYRECT trigger
     can be told from a plain register write.
2. **Core-0 task stamps in the real-CPU loop.** `C0_STAMP`/`C0_ITER` (core0_stats.h) are used only
   in the emulator loop (main.c:1851–1893).
   - Add the same stamps to the real-CPU loop (main.c:1932–1965), around:
     - `measures_thread`
     - `interrupt_thread`
     - `rtg_thread`
     - `reset_thread`
     - `ethernet_thread`
     - `usb_thread`
     - `other_tasks`
   - Keep the rate-limited report. In the report, show per task the total, the maximum and the
     count over the interval.
   - Also count how many consecutive loop turns `rtg_thread` finds a pending bus access
     immediately after an ack.
3. Run M1 case (a) against that build.
   - If H1 holds, the maximum after each op is in a non-rtg task, and the time per op matches
     M1(a).
   - The candidate fix: after an ack, let `rtg_thread` poll REG1 briefly (a few µs) before
     yielding, so the next access of a 68k burst is served in the same turn. Or skip the
     housekeeping tasks while a blitter burst is running.

### M1 on metal, and reconciling it with MG11/MG12 (2026-09-29)

**M1 results (first build 2bb6c0a4, A4000T, RT07c, two runs; `2026-09-28-mg/m1/`):**
- a: 76–77 µs per 1x1 fill;
- b − a: 6.4–7.8;
- c: +1 ns per pixel;
- d − a: 2611–2773;
- e: 92–102 (copy 1x1);
- f: +3–4 ns per pixel.

**The "2.7 ms per ioctl" was the tool, not the driver.** The first build read the clock after every
ioctl. For d (one record per ioctl) that is one `gettimeofday` per record; in the batched cases it is
one per 1024. The fixed build (398e4e29) reads the clock once per 1024 records in every case and
times `gettimeofday` alone (g). On the rig it measured `gettimeofday` at **1017 µs per call**, and
the old d − a there was 1081. With the fix the rig's d − a is 82 µs per ioctl. So metal's 2.6–2.8 ms
is almost certainly the clock too; the M1 rerun with the fixed build gives the real per-ioctl cost.

**How the DDX batches (answer: per X request, not per band).**
- The queue holds 64 records and is flushed only before CPU drawing and when an entry point
  returns, i.e. once per X request (and when full).
- x11perf `-rect100` sends 36 rectangles per `PolyFillRect` (x11perf do_tests.c:202–205). Banded
  at 8192 that is 72 records, 2 ioctls per request; unbanded it is 36 records, 1 ioctl.
- `-scroll100` and `-copywinwin100` send one `XCopyArea` per request (do_blt.c:95). Each is one
  ioctl, of 2 records banded or 1 unbanded. They are one ioctl per request by protocol, banded or
  not, so banding does not add ioctls there.
- The full-screen stress op is 134 records in 3 ioctls banded, 1 record in 1 ioctl unbanded.
- **There is no one-ioctl-per-band path.** A server-side fix would be batching across X requests,
  which the protocol's ordering makes unsafe without a flush before every CPU draw; that is the
  rule the queue already keeps.

**The register-write cost does not reconcile yet.**
- M1 puts one write at ~7 µs: b − a, one changed register (Y1) per record.
- MG11 → MG12 (cache: ~9 → ~2 writes per fill band) saved only 1.27 ms per 134-band op
  (9.5 µs per band), where 7 × 7 µs = ~49 µs was expected.
- Explanations the data allows:
  - (i) the cost of an access depends on where it falls relative to the previous op (H1: the first
    access after an op waits out the firmware's housekeeping turn, later ones are cheap). Then b's
    Y1 write is expensive and the stress's second to ninth writes were cheap, so the cache removed
    cheap writes;
  - (ii) the stress op is dominated by work that is not per band (unbanded 20.42 ms/op against
    ~1–4 ms of card time by M1's per-pixel figures, so most of it is X/client time), which makes
    band-level savings look small.
- The fixed build's case h (four registers change per record, (h − a)/4) tests (i). If h − a is
  ~4 × b − a, writes are linear and (ii) explains MG12. If h − a is much less than 4 × (b − a),
  the first access after an op carries the cost (i), which points at the firmware (M2).

**What M2 should confirm:** the core-0 stamps around the real-CPU loop's tasks, with the ack profile
by `zaddr`. The prediction under H1: after each FILLRECT/COPYRECT ack, the next rtg_thread turn comes
only after a long non-rtg task (IIC in `measures_thread`, ethernet, USB, `other_tasks`). Plain
writes that follow an ack within the same burst are served in a few µs.

The release ships as pinned; 8192 stays.

## RT-12: Workspace → Exit ends the session (2026-09-30)

**The bug.** MG13 on metal: OpenLook's own exit, Workspace → Exit → Yes, went through olwsm, which
closes every client and ends. olwm stays, idle in select, waiting for a session manager to tear it
down. xinitrc-rtg waited only for olwm, so the server, olwm, the session script and startx-rtg
stayed up (150 s+). The user was left on a grey root, with the console owned by X.

**Fix: xinitrc-rtg.** The session also ends when olwsm does.
- The script polls olwm and olwsm every 2 s. When olwsm is gone, it sends olwm SIGTERM and returns,
  and startx-rtg then stops the server as on any session end.
- An olwsm that dies within its first 20 s failed to start, and is ignored.
- The Bourne shell has no `!`, so the tests use `kill -0` and `||`.
- startx-rtg, the RT-10/RT-11 keyboard path and the SIGTERM path are unchanged.

**Rig** (copy of Installng's cut-4 disk, RT07c; evidence `2026-09-30-rt12-rig/`).
The clicks are real emulated Amiga mouse events through amiberry's IPC `SEND_MOUSE`, in steps of 3
counts to stay under the X server's pointer acceleration (threshold 4, ×2).

| case | session file | result |
|---|---|---|
| Workspace → Exit → Yes, run 1 | new | ends within 40 s; "stopping the server"; startx-rtg returns; console typing works |
| Workspace → Exit → Yes, run 2 | new | same |
| SIGTERM to olwm | new | ends within 20 s; console typing works |
| Workspace → Exit → Yes (control) | old 88a0d4f8 | **hangs**: olwm, Xzz9000 and startx-rtg alive after 180 s, olwsm and xclock gone (MG13 reproduced); a SIGTERM to olwm recovers it |

The new xinitrc-rtg is in x11/KIT.sha256. startx-rtg (e948dbf4), the server (4f2c90dc) and
rtg-kbdback (f636e9df) are unchanged.

### M1b on metal: the clean per-record figures (2026-09-30)

Fixed build 398e4e29, A4000T, RT07c, no X, two runs (`2026-09-28-mg/m1b/`):

| quantity | run 1 | run 2 |
|---|---|---|
| a, fixed cost of one fill op | 76.37 µs | 76.37 µs |
| one register write, b − a | 6.82 | 7.80 |
| one register write, (h − a)/4 | 7.00 | 7.06 |
| fill, per pixel | 0.0011 | 0.0011 |
| one ioctl, d − a | 200 | 217 |
| copy fixed cost (e) | 92.06 | 92.06 |
| copy, per pixel | 0.0043 | 0.0042 |
| gettimeofday | 2327 | 2288 |

**Reading:**
- **Register writes are linear, ~7 µs each.** (h − a)/4 matches b − a, so explanation (i) above
  ("the first access after an op carries the cost") is ruled out: every access costs about one
  firmware main-loop turn.
- **The largest term is the fixed cost of an op:** 76 µs for a fill, 92 for a copy. That is the
  firmware's per-op path; the register writes and pixels are small against it. M2 is what
  decomposes it.
- **An ioctl costs ~0.2 ms.** It is paid once per X request, not per band. Scroll and copywinwin
  pay it on every request by protocol (one XCopyArea each).
- **MG11/MG12's stress-derived "per band" figures are too coarse to judge the cache.** They are a
  full-screen time minus an unbanded baseline from another session, so a few ms of per-op noise
  becomes ±20 µs per band.
  - By M1b the cache takes a fill band from ~76 + 8×7 = 132 µs to ~76 + 1×7 = 83 µs, and a copy
    band from ~92 + 10×7 = 162 µs to ~92 + 2×7 = 106 µs.
  - x11perf rect100 barely moved (4310 → 4360). So under x11perf the X server's own work per
    request dominates, or overlaps the card time. The rig cannot show it; a metal A/B with
    zz9k_blitbench-style records does.
- **1.1 levers, in order:**
  - the firmware's per-op path (M2 brief above; Z3660 lane);
  - fewer ioctls for single-copy requests (nothing cheap; it follows from the protocol);
  - the band size (serial stays the constraint).

**MG13b (A4000T, 2026-09-30):** with the RT-12 xinitrc-rtg, Workspace → Exit → Yes ends the session
by mouse 2/2, with 50 s and 95 s to full teardown. Cut 4b passed.

**1.1 note: bound olwsm's wind-down.** After Yes, olwsm took 40–90 s to close the clients and exit,
before the script's olwsm-gone path could fire. That is probably save-yourself or WM_DELETE timeouts
on the R5 clients.
- The script sees olwsm only when it exits, so a bound first needs a start signal. Candidates:
  - olwsm's clients disappearing (the session's own xclock pid going away);
  - a root-window property olwsm changes when the exit begins (OL_MANAGER_STATE or a WM property;
    to be found with `xprop -root` before and after Yes on the rig).
- Then: SIGTERM olwm N s after that signal (N about 20), whether olwsm has finished or not.
- It must not fire on an xclock the user closes by hand. Prefer the root-property signal if one
  exists.

**RT-10 against Antti's ISSUE-74 (xrtg-amix eeea556, 2026-09-30).** He confirmed the
same console-keyboard bug on the VA2000 by reading the kernel's `activescreen` live: 0 after exit
before his fix, screens[0] after.

- **(a) Ordering: ours is safe, and the upstream PR text should say why.**
  - A hand-back placed after the chained CloseScreen does nothing if it reads a per-screen record
    that the chained close has cleared: the guard is false and the fd is gone.
  - In our `rtgCloseScreen` the chained CloseScreen runs first (rtgInit.c, `ret =
    (*pScreen->CloseScreen)(...)`). But the hand-back takes its guard and fd from `amixFbs[i]`, the
    global array in amixInit.c:93, which that close does not touch.
  - It issues `SIOCBACK` before `CloseScreen(amixFbs[i].fd)` and before `xfree(pRTG)`.
  - The rig and metal proofs (RT-10 table, MG12-kb) confirm it.
  - For the ZZ9000 upstream PR: "the hand-back must run before the input screen's descriptor is
    closed, and must not depend on state the chained CloseScreen frees".
- **(b) An open race under a display manager (not tested here; we run none).** With XDM
  respawning the server within a second, Antti still catches `activescreen` 0. That is the window
  between the old generation's SIOCBACK and the new server's SIOCACTIVATE.
  - If xdm or an auto-restarting session is ever tested, look there: read `activescreen` with od
    on /dev/mem across the respawn, as he did.
  - Probably harmless: the new server claims the screen a moment later. But a key typed in the gap
    is dropped.
- His VA2000 blitter wait is bounded but not banded, with ~0 serial traffic on his path. That is
  no concern for ours.
