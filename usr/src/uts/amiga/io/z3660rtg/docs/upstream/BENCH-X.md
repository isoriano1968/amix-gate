# BENCH-X.md — `Xzz9000` on the emulated Z3660 RTG (kerntools' LC060 rig)

> **Status (2026-09-26).** Passed on the bench 2026-09-18 (`docs/X-INSTALL-RTG.md` §4) and since on metal (`docs/DEMO-RTG.md` row 7).
>
> **Out-of-repo citations.** Evidence directories are cited by their date-stamped names. They, sibling repositories and archived images are kept outside this repo and are not needed to build or use the driver.

**For the amix-kerntools build rig.** The cross-built X11R6.3 server (`docs/X-CROSS.md`) on the
same rig that smoked the candidate kernel: LC060 bed, Z3660 RTG stand-in (amiberry 814d25c+),
the r13+zz9000 candidate kernel (or 56324/40217 with the driver — any kernel with the Z3660
row), `chipset=aga`. The bench image must carry the **stock X11R5** tree the server relies on
at run time: `/usr/X/lib/fonts/{misc,Xol,75dpi}` (SNF; **`Xol` is required by `olwm`**),
`/usr/X/lib/rgb.{dir,pag}` (dbm), and the R5 clients `olwm`, `xterm` (and `xinit`'s `.xinitrc` conventions) — if the rig root lacks them,
use the desktop image.

Inputs (from this repo, paths stated when the binary exists):
- `Xzz9000` — the cross-built server (dynamically linked: `libsocket.so libnsl.so
  /usr/amiga/lib/libscreen.so /usr/lib/libc.so.1`; everything else static). Install to
  `/opt/amix/bin/Xzz9000`, mode 755, root-owned.
- `x11/startx-rtg` (amix-z3660rtg) — the session starter; install to `/opt/amix/bin/startx-rtg`.
- The driver tools set already on the card image (`zz9k_mode` etc.).

Evidence dir `<date>-060-x-smoke/`; the report comes back to this repo.

## X0 — preconditions (transcript)

On candidate 2 (driver bytes 7e5c0e8) the boot shows **three** zz9000 lines on the rig: the
autocon row declining amiberry's piscsi board at `0x40000000` ("outside the identity map" +
the advice), then `zz9000: Z3660 RTG (fixed base) at 0x10000000 size 0x2E00000 hw 0xFFFF fw
0x103`. All expected.

```
ls -l /dev/zz9000                              # crw 49,0
zz9k_mode /dev/zz9000                          # the driver answers (mode line)
ls /usr/X/lib/fonts/misc | head -3             # PCF fonts present (fonts.dir too)
ls -l /usr/X/lib/rgb.dir /usr/X/lib/rgb.pag    # the dbm colour database
ls -l /usr/X/bin/olwm /usr/X/bin/xterm         # R5 clients
file /opt/amix/bin/Xzz9000                     # ELF 32-bit MSB m68k, dynamically linked
```

## X1 — the server alone

```
ulimit -v 131072                    # the 46 MB framebuffer mapping needs it
/opt/amix/bin/Xzz9000 :0 -mode 1024x768 -fp /usr/X/lib/fonts/misc/,/usr/X/lib/fonts/Xol/,/usr/X/lib/fonts/75dpi/ -co /usr/X/lib/rgb -ac -kb > /tmp/Xzz9000.log 2>&1 &
sleep 20; cat /tmp/Xzz9000.log; ls -l /tmp/.X11-unix/
```
Expected in the log (from `zz9000hw.c`/`rtgInit.c`): the probe, `ZZ9000InitHW: done 1024x768
stride 2048`, no `ZZ9000InitHW: mmap failed`, no font-path errors (`could not open default
font 'fixed'` = the R5 fonts are not where `-fp` says — fix the path, not the server); the
socket `/tmp/.X11-unix/X0` exists. **Frame X1**: the RTG output shows the X root (stipple or
black) with the cursor. The stand-in's access log: the driver's mode-set writes, then only
framebuffer traffic.

If the server dies with a signal, note the signal and the last log line; a crash *inside*
the server is the first real cross-build finding (the toolchain's codegen) and stops the lane
with the exact backtrace question for isoriano.

## X2 — a client from the R5 tree

With X1 running: `DISPLAY=:0 /usr/X/bin/xterm -geometry 80x24+20+20 &` then
`DISPLAY=:0 /usr/X/bin/xsetroot -solid steelblue`. **Frame X2**: an xterm window on the RTG.
Type into it via the rig's keyboard if you have one (the DDX takes keyboard/mouse through the
screen manager — a first exercise of that path on the stand-in; if input does not arrive,
say so: that is a finding for the DDX/rig, not the build).

## X3 — the session as a user would start it

Kill X1's server. Then `/opt/amix/bin/startx-rtg 1024x768` as root with a minimal
`~/.xinitrc`:
```
xsetroot -solid steelblue
xterm -geometry 80x24+20+20 &
exec olwm
```
Expected: the script prints the server line, waits for the socket, runs the session; **Frame
X3**: olwm decorations + the xterm on the RTG. Ending olwm (or `kill`) ends the session and
the server; the script prints `session ended`. Then `startx-rtg 1280x800` (the Z3660's id 17)
for **Frame X3b**, and `startx-rtg 1920x1080` for **X3c** if the stand-in copes.

## X4 — the refusals

`startx-rtg 1920x800` must fail cleanly: the server's SETMODE gets EINVAL from the Z3660 row,
the log says `SETMODE 1920x800 failed`, the script reports `server exited early`.
`startx-rtg 640x400` → `unknown mode` from the script (not in the server's table).

## Pass criterion

X0 all present; X1 server up with the expected log and a frame; X2 an R5 client on the RTG;
X3 the user's flow with olwm + xterm at 1024x768 (frames); X4 both refusals clean. Any server
crash or a wrong picture (colours, stride) is a finding: keep the log and the frame, stop
that case, report the exact line.
