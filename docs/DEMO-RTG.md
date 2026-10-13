# RTG for the demo: what it takes, what it shows, the risks

> **Status (2026-09-26).** Initial working version, **metal-proven end to end**: the candidate-2 kernel (driver 7e5c0e8) + the cross-built `Xzz9000` brought the OpenLook desktop up on the Z3660 HDMI from a real A4000 (metal session 2, rows 0–7, below; status of record 2026-09-19). Performance is poor; it is the next lane. Whether ZZ9000 and Z3660 stay one driver is still open. This page is the 2026-09-16..18 record; its "no X" and "recommendation" sections are dated and were overtaken by session 2.
>
> **Out-of-repo citations.** Evidence directories are cited by their date-stamped names. They, sibling repositories and archived images are kept outside this repo and are not needed to build or use the driver.

One page, 2026-09-16 night → morning. Everything here is bench- or metal-proven
unless marked *estimate* or *not built*. Bench (amiberry Z3660-RTG stand-in, kerntools rig)
and metal (A4000D, real LC060, r13) both PASSED tonight; the frames are in the evidence dirs. Sources: DESIGN.md §6/§11, the bench evidence dirs
`2026-09-16-zz9000-rig-step{0,1,2,3}/`, the metal probe `2026-09-16-z3660-rtg-metal/`,
the 68040/68060 port's `2026-09-15-rtg-relink-brief/ANSWERS.md`.

## What it would show (no X — `Xzz9000` is blocked on the native compiler) *(as of 2026-09-16; X since cross-built and metal-proven, row 7)*

On the Z3660's HDMI (a hand-flipped HDMI switch; the RGB console stays on the RGB capture):

1. **Pictures at 1920x1080 (or 1280x800):** the RGB565 bar pattern, the fill/copy composition,
   the driver's console test page — `zz9k_demo`, each held 20 s. **Bench-proven** (DEMO lane
   frames `D3-demo-bars.png`, `D3-demo-later.png`); **the bars metal-proven** on the Z3660 HDMI
   (`frames-r/f01…`).
2. **Live text on the RTG screen:** any command's output piped through `zz9k_cat`
   (`ls -l / | zz9k_cat`, `sh -i 2>&1 | zz9k_cat` — a shell whose output is on RTG while the
   keyboard stays on the console). Needs no kernel hook. **Bench-proven** — `D2a-zz9k_cat-rtg.png`:
   `uname -a`, `date` and `ls -l /` rendered in the console font at 1920x1080 (the text uses
   about half the width; the 1280x800 console option exists for larger type).
3. **A real login shell on the RTG screen** — the displayed VT's output through the console
   hook. On the bench kernel the source hook (`console=zz9000consolewrite`) had its first on-box
   run tonight: redirect enables and the renderer draws (`D2b-c1c-hook-rtg.png`); a *typed* VT
   session needs a keyboard on the VT (the rig is telnet-only). On the 060 the equivalent is the
   68040/68060 port's `scrcon_put` asm override (*not built*; not needed for 1–2).
4. Kernel messages on RTG through the `conputc` redirect (free with 1–3).

Sequencing on the day: console-side steps on RGB → flip → RTG-side pictures → flip back; every
RTG-side step has a transcript-visible confirmation, so a run is decidable without the frame.

## What it takes

| Item | Status |
|---|---|
| The driver (`amix-z3660rtg`, branch `z3660-target`, driver bytes 389055d, tools 5fa58ba) — one source, ZZ9000 rows byte-for-byte upstream in behaviour, the Z3660 row behind `-DZZ9K_TARGET_Z3660` | **bench-proven end to end** on amiberry's Z3660-RTG stand-in: steps 0–2 PASS, step 3 robustness PASS (mode switching incl. 32-bit, console cycles, register bounds and HI/LO composition, VM-limit path, repeated probe, read/write bounds symmetric and exact), DEMO lane PASS (boot line `zz9000: Z3660 RTG at 0x10000000 size 0x2E00000 hw 0xFFFF fw 0x103`); compiles clean the port's way (Model-B sysroot, `-m68040`, `scan060` 0 hits, page geometry OK) |
| The real-060 path without any kernel: hard map at `0x10000000`, 32-bit register file, framebuffer at `+0x200000`, HDMI | **metal-proven tonight** (`tools/zz9k_memprobe -r`, `/dev/mem`, root): bars at 1920x1080 and 1280x800 on the Z3660 HDMI, firmware serial acknowledging both modes — see below |
| **A NEW 060 kernel** = the proven r13 + this driver, by a new relink pass in `amix-040-060-port` (`relink-040-zz9000.sh` between f6 and fpe from the pinned f7vs; cdevsw row 49 at `0xa7c8`; a second `parinit` wrapper for `zz9000init`; `-DZZ9K_NO_BUILTINFONT`) | **BUILT tonight, local only:** `unix-060-fpe-r13-zz9000-CARD1-ced0s1` (1 950 897 B, `sum -r` 11366, build id 68040-260915-55), driver `zz9000.c/.h` from 5fa58ba, host gates passed, scan060 0 hits with an independent decode of all 27 ext words, r13 embedded byte-for-byte (relocations differ in exactly the six row-49 records); files, logs and the **metal checklist** in `2026-09-15-rtg-relink-brief/DELIVERY.md`. **Candidate 2** (`68060-260916-51`, driver 7e5c0e8) is the current artifact after the metal attach fix; candidate 1's **emulator smoke PASSED 2026-09-17** (kerntools, LC060 bed + Z3660 RTG stand-in, `2026-09-17-060-rtg-smoke/21-candidate-result.md`): the attach line is the first `io_init` message, multiuser, `mkdev` 49, `zz9k_maptest` PASS, bars through `write(2)` and through the mapping, `zz9k_cat` text, bounds symmetric (ENXIO at `board_size` both ways), counters at +0x231c as tabled, no panic. **Metal validation is a separate session** — r13 itself never ran the port's §5/§7/§8 suites on the LC060; counter addresses move by +0x231c versus r13 |
| On the card: the kernel (second kernel file or a separate medium), `/dev/zz9000` (`mkdev.sh`), the tools in `/opt/amix/bin` (the card's convention; upstream's is `/usr/amiga/bin` — the wrapper scripts now find `zz9k_test` in either, X-INSTALL-RTG §0) | no preset or firmware change — `autoconfig_rtg NO` is what the driver wants; ZTop/firmware stay F492BEC1 |
| Runtime: the framebuffer mapping (46 MB) exceeds the login shell's 16 MB VM default | solved in the tools (`zz9k_test` raises its own limit; `zz9k_demo` too); X would need `ulimit -v` in its startup |
| Someone at the HDMI switch during the RTG blocks | the demo's own choreography |

## The risks

1. **A new kernel on the demo machine.** Everything else on the card is validated on r13; the RTG
   kernel is r13 + one driver + one wrapper, re-validated only as far as the port's floor
   (`nm -u` clean, host gates, identity/magics, the eleven-test battery, the 060 rows, boot to
   multiuser, the driver's attach line, one draw). The port's platform rule caps real-CPU runs at
   15 minutes and forbids unattended boots. **Mitigation:** keep r13 as the default kernel; the
   RTG kernel is a second file chosen only for the RTG block.
2. **The boot-time presence read.** The driver's first board access at `io_init` time is a 32-bit
   read of `0x100001A0` in supervisor mode; on a machine where nothing answers that is a panic,
   not "no board". **The metal probe made exactly that access first, kernel-executed, and it
   answered `00000103`** on preset 7; the same page is read by `zen0` on every boot. Retired for
   this machine and preset.
3. **The register page is shared with the ethernet driver** (its doorbells at `+0x104..`). The
   driver never writes them and, from 6137966, never maps that page to userland; `zen0` keeps
   working while RTG is up — *to be shown on the bench (DEMO lane) and on metal (a `ping` while
   the bars are up)*.
4. **The user mapping of the framebuffer is unproven on r13 — neutral, not negative.** The metal
   bars were drawn through `read`/`write` on `/dev/mem` (kernel-executed); the `mmap` variant's
   self-test refused its mapping, and the 68040/68060 port identified why: the port's known
   `/dev/mem` defect (ISSUE-46, `KNOWN-ISSUES.md:4410-4470` — `mmmmap` rounds its page frame up,
   every `/dev/mem` mapping lands one 4 KiB page high; `2f004eb9` is `.text+0x1000`), which is
   fixed only in the fresh `relink-040.sh` base and **not in the r13 lineage or the candidate**.
   `/dev/zz9000` never goes through `mmmmap` (`zz9000mmap` truncates via `phystopfn`, like the
   unaffected `scrmmap`/`va2000mmap`), so the row is simply untested. The user mapping's cache
   class is settled: **NCS** (noncacheable, serialised) on r13, candidate 2 and candidate 3 — an
   unmanaged device page takes `Lcm_dev`, an unconditional `oril #0x40` in those binaries, which
   carry no `hat_cm_fb` table; `hat_cm_ram` applies to managed pages only (68040/68060 port,
   `2026-09-27-po05-prep/HAT-CM-FB-R13.md`, 2026-09-27 — corrects this row's earlier
   "may be copy-back"). `/dev/mem` mmap must not be used as an oracle on any r13-lineage kernel; nothing in
   the demo needs it, and the 68040/68060 port recommends leaving r13's bytes alone. **Mitigation, both built:** `zz9k_maptest` is
   checklist row 1 (mmap-vs-`read(2)` coherence and aliasing, PASS/FAIL); and every demo picture
   has a kernel-side path that needs no mapping — `zz9k_test … rw` draws the bars through
   `write(2)`, the console page, `zz9k_cat` and the FILL/FILLRECT/COPYRECT ioctls are kernel
   writes. Only the mmap bars and a future X server depend on that row.
5. **Display ownership.** The Z3660 has no switch back: once the driver sets a mode the HDMI shows
   RTG until reboot (the firmware splash is gone). Harmless for the demo's RTG block; the RGB
   console is untouched.
6. ~~Unproven pieces on the bench~~ — all run tonight: the `c1.c` hook (first on-box run,
   renders), `zz9k_cat` (the D2a frame), mode switching and the error paths (step 3), the
   read/write bounds (exact, symmetric). No driver finding remained open.
7. **Time.** ~~Kernel day~~ (done tonight) + a metal session + the demo card rewrite tomorrow: the RTG kernel can be
   ready to *try* by Saturday; whether it goes on the card is a Friday-evening decision on the
   metal session's transcript, not a promise.

## The metal probe (2026-09-16, A4000D, real LC060, r13 kernel, preset 7, no kernel change) — **PASS**

Two runs, operated by the Z3660 firmware side; evidence `2026-09-16-z3660-rtg-metal/`
(`probe1r-metal.txt`, `probe2r-metal.txt`, `serial-excerpt-metal.txt`, `frames-r/`); verified
by the driver side from the transcripts and the frames.

- **Before:** the Z3660 HDMI shows the firmware splash at 800x600 — "Firmware 1.03 BETA 21,
  preset 7, 060 CPU, Z3 RTG AUTOCONFIG Disabled" (`00-before-224400.jpg`) — Q6 answered, and the
  hard-map preset the driver expects.
- **Run 1 (`-r 30 5`, 1920x1080):** `FW_VERSION @100001a0 = 00000103 PRESENT`, `FW_BETA = 15`,
  `FW_ALFA = 0`, unmapped `0x000` and write-only `MODE` read `ffffffff`; framebuffer
  write/read self-test on the card's RAM `MATCH`; the driver's mode-set sequence as 4-byte
  writes; `MODE = 00000105`; `row 182 = f800f800`, `row 0 = 00000000`; the firmware's serial
  acknowledged `video_mode_init: 5 color: 1 scale: 0 / 1920x1080 16 bit color, 60 Hz`; **the
  HDMI switched to 1920x1080@60 and showed the bars, then solid blue, then the bars**
  (`f01…f44`). Colours correct → the byte order of 16-bit stores on the FPGA path is right (A1).
- **Run 2 (`-r 20 17`, 1280x800):** same, `MODE = 00000111`, serial `video mode: 1280x800 16 bit
  color, 60 Hz`, the capture locked at 1280x800 (it reports 56 Hz) and showed the bars (`g01…`).
- No `write unaligned` or abort lines on the serial; the box answered on telnet and serial
  throughout; clean halt, preset 7 left selected; firmware untouched.
- **Caveat carried into the design:** run 1 of the *mmap* variant showed that a user `mmap()` of
  `/dev/mem` on r13 lands one 4 KiB page high (the port's ISSUE-46 in `mmmmap`, not fixed in the
  r13 lineage; self-test refused it; the card was never touched) — the working path was the
  kernel's own copy loop. `zz9000mmap` is a different `d_mmap` (unaffected producer); its metal
  validation is `zz9k_maptest` before anything draws through it (DESIGN.md §11.5).

**What this proves without any kernel:** the real-060 path — the `autoconfig_rtg NO` hard map at
`0x10000000`, the 32-bit register file and its presence signature (the driver's first access),
the framebuffer at `+0x200000` with correct 16-bit byte order, mode set to both demo
candidates, and the HDMI output — works exactly as the driver's Z3660 row assumes.

## The RTG card on metal — session 1 (2026-09-18) and what session 2 must show

**Session 1: kernel green, driver red.** The port's candidate kernel booted the RTG demo card
on the real 060 (banner `68060-260915-55`, root on `ced0s1`, multiuser, the SCSI-chooser
route), but the driver refused the board:
`zz9000: Z3660 combo board at 0x10000000 size 0x8000000 unsupported`. My error, not the
board's: the driver rejected any Z3660 that appeared in the AutoConfig table, assuming such an
entry could only mean the unreachable `autoconfig_rtg YES` placement. On real hardware the
hard-mapped board still enters the board list — it advertises itself for SCSI boot, the route
this card boots by — and the entry names its true base, `0x10000000`. Fixed in **7e5c0e8**
(attach on the base the entry names; refuse only what is genuinely above the identity map; let
a refused row fall through to the next). **No firmware or preset change: SCSI BOOT stays on.**

**Session 2, row 0: PASS (metal, 2026-09-18).** The attach is green and verbatim as predicted:
```
zz9000: autocon match pc 0x144B0001 base 0x10000000 size 0x8000000
zz9000: Z3660 RTG at 0x10000000 size 0x2E00000 hw 0xFFFF fw 0x103
```
banner `68060-260916-51`, multiuser, `/dev/zz9000` present (char major 49),
root on `ced0s1`, the preset still `AutoConfig RTG NO`. **This is the one path no bench could
exercise** — the driver matching the combo board through the AutoConfig entry and attaching at
the base that entry names — so the board-table correction is now validated where it failed.
Rows 1–4: **PASS** — `zz9k_maptest` reports the user mapping coherent (no copy-back: the last
open metal question, answered), `zz9k_test … rw 1920x1080` draws full-HD bars through `write(2)`
with the capture reading 1920x1080@60 and the registers `FW_VERSION 0x0103` / `FW_BETA 0x0015`,
`zz9k_cat` renders piped text, and the demo's three pages (bars, blitter fill/copy, console)
all render at 1920x1080. **One packaging deviation, mine:** the `zz9k_demo` wrapper hardcoded
upstream's `/usr/amiga/bin/zz9k_test` while the card ships the tools in `/opt/amix/bin`, so the
wrapper failed "not found" — content unaffected, and fixed for the next rebuild (the scripts now
locate `zz9k_test` themselves). **Row 7: PASS** — the full OpenLook desktop on the Z3660's HDMI at 1024x768: `olwm`'s Workspace
menu with its Programs submenu, a live `xclock`, `xload`, `olfm`, and two
**`olwm`-decorated** xterms with working shells; all seven clients connected. (Decorated, so the
reparenting race did not bite — the metal session used the restored busy `/.xinitrc`.) **One
launcher finding, mine:** the first attempt failed with every client reporting `Can't Open
display`, because each of my readiness probes makes the server reset and re-run its slow
initialisation — two probes plus an 8-second settle left it still re-initialising at
`serverGeneration=4`. `RTG_SETTLE_AFTER=25` fixed it on the spot; the launcher now probes once
and waits for the server's log to go quiet instead (X-INSTALL-RTG §3, sha `26234982`).
**Rows 0–7 all green.**

**Session 2 runs candidate 2** (port build `68060-260916-51`, card-1 sha `c02684b5…`, card-0
`bf3e8294…`, r13 still byte-identical inside; **counter addresses move +0x18 versus candidate 1
— use the port's `33-status-facts-c2` table, not candidate 1's**). Expected, in order:

```
zz9000: autocon match pc 0x144B0001 base 0x10000000 size 0x8000000
zz9000: Z3660 RTG at 0x10000000 size 0x2E00000 hw 0xFFFF fw 0x103
```
(the first line is the probe finding the board where AutoConfig says it is; the second is the
attach with the RTG window, not the 128 MB combo size.) Then the row order is unchanged —
`zz9k_maptest` → `zz9k_test … rw` bars → `zz9k_cat` text → `zz9k_demo` → **`startx-rtg` last**,
because X is the only step that depends on the user mapping being coherent, which is still the
open metal row.

**The `dd`-on-`/dev/mem` "hang" of session 1 is not a finding — it is my diagnostic command.**
This box's `dd` *reads* its way to a `skip=` point on a character device instead of seeking:
our own bench evidence shows it (`2026-09-16-zz9000-rig-step2/23b-…txt`: `dd: read error
during skip`, on a different kernel and a different device, so it is a property of the box's
`dd`). My command was `dd if=/dev/mem bs=4 skip=67108864` — that asks for **67 million 4-byte
reads** before the one I wanted; seven minutes of CPU and still running is exactly what that
costs, not a stuck bus. The reason last night's probe read the same addresses in milliseconds
is that `zz9k_memprobe` uses `lseek()` + `read()`. Nothing points at the relinked kernel:
`read(2)` on `/dev/mem` does not go through `mmmmap` (so not ISSUE-46), and the driver's
rejected probe touched no board register — the REJECT path printed and returned without an
access. **Recommendation: no investigation, no port time.** One command settles it in
session 2 if anyone still wants certainty (below).

**Session-2 canary (new, cheap).** `zz9k_memprobe -q` (added 2026-09-18) does an `lseek` plus
one 4-byte read of `FW_VERSION`/`FW_BETA` through `/dev/mem`, prints
`the board ANSWERS at 10000000`, exits 0, and **touches no mode and writes nothing**. Use it:
- **only if the attach line is red again** — it separates "the board does not answer" from
  "the driver's logic is wrong", which is precisely the question session 1 had to guess at;
- and as the one-command disproof of the `dd` story, since it reads the very addresses that
  "hung" and returns at once.
It cannot be a *pre-attach* canary: the driver probes at `io_init`, long before userland. If
the attach line is green, skip it — `zz9k_maptest` and everything after go through
`/dev/zz9000`, not `/dev/mem`. A board that genuinely stopped answering would hang those too,
which is why the row order keeps them early and `startx-rtg` last.

**Honest limit: the board-table fix can only be validated on metal.** On the bench the autocon
row matches amiberry's piscsi model at `0x40000000`, is declined, and the *fixed-base* row
attaches the stand-in — so the rig exercises the fall-through but never the attach-by-autocon
path that failed here. A green bench run of candidate 2 therefore proves no regression; only
the card's own boot lines prove the fix.

## Recommendation (driver side, 2026-09-16 — carried out as session 2 above)

The metal probe drew the bars at both demo resolutions on the real Z3660 HDMI, through the
exact accesses the driver makes, with the firmware acknowledging each mode on its serial; the
r13+RTG kernel exists and passed the port's host gates. The remaining metal unknown is the
user mapping (`zz9000mmap`) on r13, with an instrument and a mapping-free demo path both in
hand. Recommendation: **a metal session with the candidate kernel after the demo card is
rewritten, in this order — boot line, `zz9k_maptest`, `zz9k_test … rw` bars, `zz9k_cat` text,
`zz9k_demo` — keep r13 as the card's default kernel regardless; the demo's RTG block is
`zz9k_demo`/`zz9k_test rw` + `zz9k_cat` on the Z3660 HDMI (someone at the switch), no X.** The picture a
viewer would see is `frames-r/f01…`.
