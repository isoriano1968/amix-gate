# BENCH-DEMO.md — the no-X demonstration picture, rehearsed on the stand-in

> **Out-of-repo citations.** Evidence directories are cited by their date-stamped names. They, sibling repositories and archived images are kept outside this repo and are not needed to build or use the driver.

**For the amix-kerntools build rig.** This rehearsal produces the frames that decide whether
RTG is shown in a demo. It is the **first on-box run of the `c1.c` console hook**
(`console=zz9000consolewrite`), so it needs a **fresh kernel**: step-2 record + `console=`,
driver bytes **6137966** (adds `ZZ9KIOC_CONPUTS`, the 060-safe console test, and moves the Z3660 row's device space to the framebuffer alone — `fb_offset 0`, `board_size 0x2E00000`; tools from the same revision: `zz9k_cat`, `zz9k_reg`, `zz9k_demo`, `zz9k_test` raising its own VM limit), `make=ZZ9K_TARGET=-DZZ9K_TARGET_Z3660,ZZ9K_BENCH=-DZZ9K_BENCH_Z3660`,
`cflags_strip=-fno-builtin` kept, new sha pin; rig = the step-2 config (stand-in, `chipset=aga`
+ `chipset_compatible=A3000`). Optionally a second kernel with
`…,ZZ9K_OPTS=-DZZ9K_Z3660_CON_1280x800` to compare console text sizes (only
if time allows; the 1920x1080 frame comes first).

What the demo would show on the Z3660's HDMI (the firmware splash until then):
1. **Live text on the RTG screen, two ways.** (a) `zz9k_cat`: any command's output piped into
   the framebuffer console through the new `ZZ9KIOC_CONPUTS` ioctl (`ls -l / | zz9k_cat`;
   `sh -i 2>&1 | zz9k_cat` gives a shell whose output lands on RTG while input is typed on the
   telnet/serial side) — **this path needs no kernel source hook and is what a relinked 060
   kernel could also do.** (b) the `c1.c` hook: after `zz9k_console start` the displayed VT's
   own output lands on the RTG console (`sp == displayedscreen` gate) — a real login shell on
   the VT, but only in a kernel built from source with the hook (this bench kernel; not the
   060 relink).
2. **The test pictures** — `zz9k_demo` (bars 1920x1080, the fill/copy test, the console test
   page), each held 20 s, with the VM limit raised inside the script.

Evidence dir `<date>-zz9000-rig-demo/`; the report comes back to this repo.

## D1 — boot and the hook's presence

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

Boot line (bytes before 7e5c0e8): `zz9000: Z3660 RTG at 0x10000000 size 0x2E00000 hw 0xFFFF fw 0x103` (from 6137966 the
reported window is the framebuffer alone: `fb 0x0+0x2e00000` in `zz9k_test`; the register page is
no longer readable through `read(2)`/`mmap`, only through GETREG).
The kernel build log must show `c1.c` patched (the generator's gate insert) and the link must
resolve `zz9000consolewrite` (nm on the kernel: `T zz9000consolewrite`). Nothing is on the RTG
screen yet except the stand-in's "native" view — the hook is inert until redirect is enabled.

## D2a — text on RTG through `zz9k_cat` (works on any kernel with the driver)

On the telnet console, as root:
```
uname -a | zz9k_cat                  # enables the console redirect itself; text appears on RTG
(date; ls -l /; cat /etc/motd) | zz9k_cat
. /usr/amiga/bin/zz9k_colorls_env; colorls -l /usr/amiga/bin | zz9k_cat   # ANSI colours
sh -i 2>&1 | zz9k_cat                # a shell: type on telnet, output on RTG; `exit` ends it
```
**Frame D2a-1** after the first three; **Frame D2a-2** with the shell showing a couple of
commands. Transcript: the telnet side (which shows only what was typed). Then `zz9k_console
stop` is a no-op for the picture (no switch back) — note it.

## D2b — the VT shell on RTG through the `c1.c` hook (this bench kernel only)

On the telnet/serial console, as root, with the tools built from f5799f9 and installed
(`make install-tools`):
```
zz9k_console start          # -> "ZZ9000 console redirect enabled. Native RGB drawing should stop."
```
From now on the displayed VT's output goes to the RTG screen (the stand-in shows RTG after the
first MODE write). On the **VT itself** (the amiberry window's Amiga keyboard — or, if the rig
is headless, note that telnet output is NOT the VT and will not appear on RTG; use the IPC
keyboard to type on the VT), log in and run:
```
uname -a
date
ls -l /
cat /etc/motd
```
**Frame D2-a** (1920x1080, `SCREENSHOT`): the login banner and these outputs in white-on-black
sunfont text on the RTG output; the cursor visible. **Frame D2-b**: after `/usr/amiga/bin/colorls
-l /usr/amiga/bin` with `. /usr/amiga/bin/zz9k_colorls_env` — colours through the ANSI SGR path.
Transcript: the same commands' output on the VT capture (they must match the frame).
Then `zz9k_console stop` (= `noconsole`) → `zz9000 console disabled`; on the Z3660 row this
does **not** switch the RTG output back (no capture path): the last frame stays. Note it.

If telnet is the only keyboard: `zz9k_test /dev/zz9000 consoletest` after `start` still
proves the path (the kernel prints the test page through the same renderer), and D2 becomes
"kernel-driven text on RTG" — say so in the report; it must be clear whether a live
shell was actually shown.

## D3 — the pictures

```
ulimit -a | grep memory          # record the default (16384)
/usr/amiga/bin/zz9k_demo /dev/zz9000 20 1920x1080
```
**Frames D3-a/b/c**: bars, the fill/copy composition, the console test page, each while held.
The script's own lines (`zz9k_demo: bars 1920x1080 for 20 s` …) and the three `zz9k_test`
transcripts in the evidence. No `mmap:` line (the script raises the limit). Exit 0.

## D4 — what the viewer sees in sequence (dry run of the demo script itself)

`zz9k_console start; zz9k_demo /dev/zz9000 20; zz9k_console start` — the RTG output shows:
shell → bars → fill/copy → console page → shell again (the second `start` re-clears and
re-enables the console). **Frame D4**: the final state. Record the total wall time.

## Pass criterion

D1 boot line and `T zz9000consolewrite`; D2a two frames of piped text/shell output on RTG;
D2b frames showing VT text on RTG (or the explicit "kernel-driven only" statement); D3 three
frames and no mmap error; D4 final frame. The judgement
"is the 1920x1080 console text readable as a demo picture" is made from the frames, which are the
deliverable, at full resolution, plus one downscaled overview if the tooling makes it easy.
