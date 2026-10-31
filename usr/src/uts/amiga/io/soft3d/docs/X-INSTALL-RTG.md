# X-INSTALL-RTG.md — putting X-on-RTG into the desktop image

> **Status (2026-09-26).** Initial working version, **metal-proven end to end**: the candidate-2 kernel (driver 7e5c0e8) + the cross-built `Xzz9000` brought the OpenLook desktop up on the Z3660 HDMI from a real A4000 (metal session 2, rows 0–7, `docs/DEMO-RTG.md`; status of record 2026-09-19). Performance is poor; it is the next lane. Whether ZZ9000 and Z3660 stay one driver is still open.
>
> **Out-of-repo citations.** Evidence directories are cited by their date-stamped names. They, sibling repositories and archived images are kept outside this repo and are not needed to build or use the driver.

Mechanical recipe: what to copy, where, with which modes, and how the session is wired, so
that a console login on the chipset screen can type `startx-rtg 1024x768` and get `olwm` +
`xterm` on the Z3660's HDMI. Nothing here changes the existing X11R5 installation; the RTG
server is an addition under `/opt/amix`. Companion documents: `docs/X-CROSS.md` (how the
binary is built), `docs/BENCH-X.md` (the bench proof, passed 2026-09-18, §4), `docs/DEMO-RTG.md`
(the metal record).

## 0. What is installed (five files, one directory)

| File | Path on the image | Mode / owner | Source |
|---|---|---|---|
| `Xzz9000` | `/opt/amix/bin/Xzz9000` | 755 root:root | `2026-09-18-xzz9000-cross/out/Xzz9000` (1 919 891 B, sha256 `709b8a97f5c0c073…`) |
| `startx-rtg` | `/opt/amix/bin/startx-rtg` | 755 root:root | `amix-z3660rtg/x11/startx-rtg` |
| `xinitrc-rtg` | `/.xinitrc-rtg` (root's home) | 755 root:root | `amix-z3660rtg/x11/xinitrc-rtg` — the RTG session, so the machine's `/.xinitrc` stays the demo's own (§2) |
| `rtg-mode` | `/opt/amix/bin/rtg-mode` | 755 root:root | kerntools' card wrapper (validates a mode name, calls `zz9k_mode`) — **not needed by `startx-rtg`**, which passes `-mode` to the server; keep it for the picture demos |
| the driver tools | `/opt/amix/bin/zz9k_*`, `colorls` | 755 root:root | `2026-09-17-z3660rtg-tools-6358112/`. **The wrapper scripts (`zz9k_demo`, `zz9k_console`) locate `zz9k_test` themselves** — `$ZZ9K_TEST`, then their own directory, then `/usr/amiga/bin`, then `/opt/amix/bin` — so either convention works and no profile variable is required. (Before 2026-09-18 they hardcoded upstream's `/usr/amiga/bin` and failed on the card: metal session 2, row 4.) |
| `/dev/zz9000` | char 49,0 | 666 (or 600) root:root | `mkdev.sh` |

**Nothing else is required.** The R6.3 server reads the box's existing X11R5 data:
- **Fonts** — its bitmap font layer accepts `.snf` as well as `.pcf`
  (`lib/font/bitmap/bitmapfunc.c:78-115`), so the stock `/usr/X/lib/fonts/{misc,Xol,75dpi}`
  trees with their `fonts.dir` are used as they are. No font is compiled or copied.
  **`Xol` is required**: it holds the OPEN LOOK fonts (`olgl*.snf`, `olcursor.snf`) and `olwm`
  exits with "Could not load default OPEN LOOK font" without them (bench 2026-09-18).
  `startx-rtg`'s default font path is `misc/,Xol/,75dpi/`.
- **Colours** — `-co /usr/X/lib/rgb` opens the stock dbm pair `rgb.dir`/`rgb.pag`
  (`os/oscolor.c:81-87`); `dbminit`/`fetch` resolve at run time from `libnsl.so`.
- **XKB** — not used: the server is started with `-kb` (the port's XKB device path is
  unfinished); no `xkb/` data directory is needed.
- **Clients** — `olwm`, `xterm`, `xsetroot` etc. are the R5 binaries already on the desktop
  image, unchanged, linked against the R5 shared libraries. The RTG server speaks the X11
  protocol to them; no client is rebuilt.
- **Shared libraries the server itself needs** (already on any Amix): `libsocket.so`,
  `libnsl.so`, `/usr/amiga/lib/libscreen.so`, `/usr/lib/libc.so.1`.

## 1. Install

```sh
mkdir -p /opt/amix/bin
# from the host: push Xzz9000, startx-rtg (and the zz9k_* set if not already there)
chmod 755 /opt/amix/bin/Xzz9000 /opt/amix/bin/startx-rtg /opt/amix/bin/rtg-mode
chown root:root /opt/amix/bin/*
sh mkdev.sh                        # /dev/zz9000 c 49 0 if not present
```
The kernel on the image must carry the driver (the r13+zz9000 candidate, or any kernel built
with `-DZZ9K_TARGET_Z3660`); `zz9k_mode` with no arguments proves the driver answers.

`/etc/profile` (or the demo user's profile) gets the search path and the tool override the
card plan already uses:
```sh
PATH=$PATH:/opt/amix/bin; export PATH
ZZ9K_TEST=/opt/amix/bin/zz9k_test; export ZZ9K_TEST
```

## 2. The session (`~/.xinitrc-rtg`, not the machine's `/.xinitrc`)

`startx-rtg` runs the first readable of `$XINITRC`, **`~/.xinitrc-rtg`**, `~/.xinitrc`,
`/.xinitrc` — with `DISPLAY` set — and stops the server when it returns (announcing that,
with the session's exit status: a server log ending after that line was truncated by that
stop, not by a fault). **The RTG file comes first on purpose: the machine's own `/.xinitrc`
is what the native chipset `startx` runs, and nothing about the RTG should require editing
it.** On the demo image root's home is `/`, so the RTG session file is `/.xinitrc-rtg`.

Install `x11/xinitrc-rtg` (755) as that file. It is the demo session — OpenLook window
manager, Workspace manager, clock — and it follows two rules, both learned on this hardware:
**start the window manager first and let it claim the screen before any client maps a window**,
and **do not return until the session is over** (the launcher stops the server when it does):
```sh
olwm &
OLWM=$!
sleep ${WMSETTLE-5}
xsetroot -solid '#4a5a6a'
olwsm &
xclock -geometry 100x100-10+10 &
wait $OLWM
```
The first rule is not theoretical: on 2026-09-18 a busy session launched its clients alongside
`olwm` and the xterms came up **undecorated for the life of the session** — no title bars, no
resize corners — while `olwm` was demonstrably alive and its Workspace menu drew normally
(`2026-09-18-r2-rtg-card/B3-decoration-final.png`). `olwm` decorates by reparenting, and a
68060 is slow enough that a client mapping during the window manager's own startup loses that
race as a matter of course. Nothing about the server or the driver is involved — the same
server decorates correctly when the window manager goes first.
Clients come from the R5 tree (`/usr/X/bin`); put that on `PATH` if it is not already. No
`sleep` is needed at the top: the launcher proves the server ready before the session runs (§3). With no session file at all the
launcher runs a bare OpenLook session (xclock, xterm, `olwsm` if present, `olwm`) and says so —
that fallback still starts its clients alongside the window manager, so it is for bring-up, not
for a demo; a session file is the supported path.

> **Next-rebuild list (2026-09-18).** In one place, for whoever builds the next RTG image:
> 1. launcher **`startx-rtg` sha `26234982`** (session-file split **and** the reset-aware
>    readiness of metal session 2 — see §3; supersedes `0f59bbe3`);
> 2. **`/.xinitrc-rtg`** from `x11/xinitrc-rtg` sha `39f29bede` (window manager first — §2);
> 3. **restore `/.xinitrc`** to the demo session (olwm + olwsm + xclock);
> 4. the **self-locating wrapper scripts** `zz9k_demo` sha `fc31079f` and `zz9k_console` sha
>    `aa78b51c` (metal session 2 row 4: the old ones hardcoded `/usr/amiga/bin/zz9k_test` and
>    failed on a card that installs to `/opt/amix/bin`; the tools themselves are unchanged);
> 5. optionally `zz9k_test` sha `086ca743` with `ZZ9K_HOLD` (non-interactive hold) and
>    `zz9k_mode`/`zz9k_maptest`/`zz9k_rw` if not already present.
>
> **Known deviation in the shipped r2 image (2026-09-18), to fix at the next rebuild.** r2's
> `/.xinitrc` was **replaced** with the bench session (`sleep 8`, xterm + xclock, `exec olwm`),
> which — because that file is shared with the native chipset `startx` — cost the RTG card's
> native desktop its Workspace manager. The next rebuild should: restore `/.xinitrc` to the
> demo session (olwm + olwsm + xclock); install `/.xinitrc-rtg` from `x11/xinitrc-rtg`; and
> carry launcher `sha 0f59bbe3` or later, which prefers the RTG file. `olwsm` on `Xzz9000`
> (an R5 client on the R6.3 server) is unproven and wants one bench pass — after the RTG
> card's metal session 2.

## 3. Starting and stopping

From a console login on the chipset screen:
```sh
startx-rtg                 # 1024x768 (default), or: startx-rtg 1280x800
```
What happens: the mode is chosen **before** the server starts (`-mode WxH`, validated by the
script against the server's table, then by the driver — a mode the attached board refuses
fails at `SETMODE` and the server exits with the reason in `/tmp/Xzz9000.log`); the script
raises its own VM limit (**the server's 46 MB framebuffer mapping fails under the 16 MB
default** — `ZZ9000InitHW: mmap failed (Not enough space)`); it waits for the display
endpoint `/tmp/.X11-unix/X0` — which is a **FIFO** here, polled with `ls`, since this shell's
`test` has neither `-S` nor `-e` — then establishes readiness and runs the session.

**Readiness, and why it is not just a sleep or a probe loop.** The X server resets when its
last client disconnects, re-running the whole `ZZ9000InitHW`/`ScreenInit` — seconds of it on a
68LC060. A probe client connects and *exits*, so **every probe costs a reset**, and probing
harder makes readiness worse: on metal (session 2, row 7) two probes plus an 8-second settle
left the server still re-initialising at `serverGeneration=4`, and every client of the session
got `Can't Open display`. The launcher therefore probes **once**, then waits for the server's
own log to stop growing (three quiet seconds, bounded by `RTG_SETTLE_MAX`, default 90) — which
is precisely the reset finishing, and adapts to the hardware instead of guessing — then applies
a short floor (`RTG_SETTLE_AFTER`, default 5). The proven workaround on the shipped
image is equivalent and needs no new file: `RTG_SETTLE_AFTER=25 startx-rtg 1024x768`. Ending the window manager ends the
session and stops the server.

**The console stays on the chipset screen throughout** — the RTG output is a separate display
and the driver's framebuffer console is not enabled by this path; `startx-rtg` does not touch
it. Keyboard and mouse reach X through the server's own screen-manager path (`OpenScreen`,
`SIOCACTIVATE`, `SIOCSETINPUTMODE SIM_RAWKEY` — `rtgInit.c:164-198`), i.e. the Amiga's own
input devices, with no configuration. After the session the RTG keeps the last picture (the
Z3660 has no switch back); `zz9k_demo`/`zz9k_test` can paint over it.

Mode names accepted by both sides on a Z3660: `640x480 800x600 1024x768 1280x720 1280x1024
1280x800 1920x1080` (the last two only with the mode-table patch, §5). `1920x800` is a ZZ9000
mode and is refused by the Z3660 row — a clean, logged failure.

## 4. Verification on the image (in this order)

**Bench-proven 2026-09-18** on a work copy of the desktop image with the emulated Z3660
RTG (kerntools, `2026-09-18-060-x-smoke/`): the server drives the RTG and runs the
full OpenLook desktop at 1024x768 — two `olwm`-decorated `xterm`s and a running `xclock`
(`X3-desktop-olwm-1024x768-FINAL.png`). The three findings of that run (the `test -S` wait,
the missing `Xol` font path, the VM limit) are fixed in `startx-rtg` and folded in above.

Console/telnet transcript, then the HDMI. The lines are what a pass looks like.
```sh
zz9k_mode /dev/zz9000                       # e.g. "800x600 16 (mode 1, fb 0x2e00000 bytes, zorro 3 product 0x01)"
zz9k_maptest /dev/zz9000                    # PASS -- user mapping coherent   (the metal-open row)
file /opt/amix/bin/Xzz9000                  # ELF 32-bit MSB m68k, dynamically linked
startx-rtg 1024x768                         # prints the server line; session starts
#   in /tmp/Xzz9000.log: "ZZ9000InitHW: done 1024x768 stride 2048", no "mmap failed"
#   on the HDMI: olwm's root pattern, an xterm; the console screen is unchanged
#   type in the xterm with the Amiga keyboard to prove the input path
# end olwm  ->  "startx-rtg: session ended"
startx-rtg 1920x800                         # must fail: SETMODE refused, "server exited early"
```
On metal these belong in the port's §9 checklist order: boot line → `zz9k_maptest` →
`zz9k_test … rw` bars → `zz9k_cat` text → `zz9k_demo` → **then** `startx-rtg` (X last: it is
the only step that depends on the user mapping being coherent, which is the metal-open row).
With the HDMI switch: the console-side lines above are read on the RGB capture; flip for the
RTG-side pictures; every X step also has a transcript line (`/tmp/Xzz9000.log`, the script's
own output) so the run is decidable without the frame.

## 5. The one source deviation

`x11/patches/zz9000hw-modes.patch` adds `1920x1080` and `1280x800` to the server's mode table
(build-tree only; upstream unchanged). Without it those two `startx-rtg` arguments are refused
by the server, and `1024x768`/`1280x720` remain available. It is a candidate for upstream.
