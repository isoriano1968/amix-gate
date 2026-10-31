# P-5 step 0 — the X measurement kit (RT-01)

Four cross-built Amix binaries plus one script. They let the RTG campaign (P-5) start from
measurements, not impressions. Built by `x11/bench/build.sh` (the recipe), which reproduces them
byte for byte. The binaries are not tracked: they live in `build/rt01/` (gitignored), and their
hashes are below.

| Binary | What | sha256 |
|--------|------|--------|
| `x11perf` | X11R6.3 x11perf, from the tree Xzz9000 comes from | `d4f9beb6d22d569ec60b6ab966b4e8edc987d9d6f0696ea543cd70217bfe47c9` |
| `xbench` | Antti's xrtg-amix `tools/xbench.c`, unchanged | `cd4ebee6078dff45e509abc11ce654481cf3de11cad93c4ddbed0e3010f6bdce` |
| `xorfill` | S1 discriminator: GXxor fill/rectangle drawn twice must vanish | `591427afb61e40fe91b31fd954332000a64734da13da6d328c59aea1747d801c` |
| `copyexpose` | S2 discriminator: CopyArea from a partly covered window | `bf6bd022b1f81a8bde532a79e7fcb814323c8c719a7b076340ef44f3d4ddbe8c` |
| `xorcopy` | S4/S5 discriminator: XPutImage, pixmap→window and window→window XCopyArea, GXxor twice must vanish | `5af9a664ff364d1129504ad686ceedc535df299ba90e746f882d55a86b4f61e7` |
| `xdrag` | XTEST title-bar drag (outline move without a mouse) | `2b9da5f175940970f007189b4e095d8ea276bcc1e5cae4033268d93d3ab9e542` |
| `blit-edge` | P-5 fix 3: fills, clears, copies and window moves read back against a client-side reference (every alignment, all screen edges, overlaps in eight directions) | `9ec5ad878fd8bf91f0104f2e4c678239625f6d5d132160b30ddd714e262d92a0` |
| `zz9k_blitbench` | per-record blitter timing through /dev/zz9000, no X (M1 of NOTES "Where the per-band time goes") | `398e4e2978bd217249d8b5a49a3022ed84d9e84ff15f2c6c3dd04fba0993ee75` |
| `x11perf-baseline.sh` | runner, test list verbatim from xrtg-amix | (tracked, this dir) |

All four link the R6.3 Xlib **statically**, so the R4/R5 X libraries on the box play no part.
They need only `libsocket.so`, `libnsl.so` and `/usr/lib/libc.so.1`, exactly like `Xzz9000`.
Every dynamic import resolves in the guest `libc.so.1`/`libsocket.so`/`libnsl.so`.

`scan060` lists 17–60 candidates per binary, of the same class as `Xzz9000`'s 124: 64-bit
`mulul`/`divsl` from gcc's constant-divisor code in the static Xlib. They are in user mode, where
the LC060 kernel's ISP emulates them. None comes from the two discriminators' own logic.
All four are built with plain `-O`, like `Xzz9000`. `-m68040` emits 68040-only FPU opcodes
(`fdmove`, `fddiv`, …). The FPU-less LC060 kernel's FPU emulation traps on them, and a first
`-m68040` xbench died with Illegal Instruction on the rig.

## Install

Copy the six binaries and `x11perf-baseline.sh` into one directory on the box, e.g.
`/opt/amix/bench/`, mode 755. Check the hashes on the box against the table if `sum` or
`sha256sum` is available; otherwise compare sizes: x11perf 609304, xbench 454010, xorfill 447789,
copyexpose 493707, xdrag 434345, xorcopy 446362, blit-edge 452641.

## Preconditions

The server is up through the normal path:

```
startx-rtg 1280x800          # or 1920x1080; olwm session from ~/.xinitrc-rtg
```

Then, from a second login on the same box (telnet or the chipset console), as the same user:

```
DISPLAY=:0; export DISPLAY
cd /opt/amix/bench
/usr/X/bin/xset s off        # REQUIRED after every server start, see below
```

**Turn the screen saver off first.** The server's default screen saver blanks after 600 s
(`xset q`: timeout 600, prefer blanking). On the rig (2026-09-27) the frame went black ten minutes
into a run. Worse, while it is active a newly mapped window managed by the window manager never
gets its first Expose, so `xorfill`/`xorcopy` (which wait for it) hang. `copyexpose`'s
override-redirect windows are raised above the saver and still run. Its verdicts were valid,
but its frame was black. Since 2026-09-27 the shipped session `x11/xinitrc-rtg` turns the saver
off itself. The per-run `xset s off` stays, for sessions started any other way.

## Smoke (the RT-01 acceptance: connects and draws)

```
./x11perf -repeat 1 -time 1 -rect10       # prints the x11perf header + one result line
./xbench -only noop                        # one line: noop <ops/s> <n>
./xorfill -hold 10                         # one window, three columns; prints PASS/FAIL per case
./copyexpose -hold 10                      # three windows; prints PASS/FAIL per case
```

Evidence: the printed lines, plus one screenshot while `xorfill` and `copyexpose` hold their
windows.

## Step 0a — the artefact record (rig first, then metal)

```
./xorfill -once -hold 20     # what the shapes look like drawn once (reference frame)
./xorfill -hold 20           # drawn twice with GXxor: a correct server leaves the window black
./copyexpose -hold 20
./xorcopy -once -hold 20     # the pattern once per column (reference frame)
./xorcopy -hold 20           # GXxor twice: all three columns must read back PASS
```

- **xorfill.** Three columns: `fill` (XFillRectangle, the suspect path), `wide` (a 3-pixel
  XDrawRectangle, which R6.3 mi turns into PolyFillRect), and `thin` (a zero-width rectangle, the
  FillSpans control). Output is `PASS` or `FAIL (N pixels left after two XORs)` per column.
  - **S1 confirmed:** `fill` (and `wide`) FAIL while `thin` PASSES. The frame shows solid shapes
    in those columns where there should be nothing.
  - **S1 refuted:** all three PASS.
  - Two more lines, `root2` and `root1`, repeat **olwm's own move outline**: `XFillRectangles` on
    the root window, four rectangles 2 px (olwm's `OUTLINE_WIDTH`) or 1 px wide, GC function
    `GXinvert`, `subwindow_mode IncludeInferiors`, drawn twice. They are judged by comparing the
    root area before and after. This is the request behind the outline-drag trail seen on the
    A4000T (709b8a97): expected FAIL there and PASS on the P-5 fix-1 server. Seven verdict/count
    lines in all.
- **copyexpose.** Case `win->win` copies a green window whose right half is under a red one
  into a blue window. Case `scroll` scrolls the green window left by 50 inside itself. A correct
  server fills the uncopyable part with the destination background and reports GraphicsExpose
  events.
  - **S2 confirmed:** `exposures 0` and/or `RED (copied the cover)`.
- **xorcopy.** Three columns: `image` (XPutImage → SetSpans, S4), `pixmap` (pixmap→window
  XCopyArea → SetSpans, S4), `window` (window→window XCopyArea, the fast path, S5). Each is
  done twice with GXxor and must leave the column black. `FAIL (N pixels left …)` means the
  server drew it as GXcopy. On 709b8a97 all three are expected to FAIL; the p5-3/p5-4 server
  must PASS all three.
- Then the drag case, for the frame and the position of the leftovers: in the olwm session,
  drag an xterm by its title bar (outline move) across the screen and release. Without a hand
  on the mouse, use `./xdrag -hold 20 xterm`. It finds the window by WM_NAME, presses
  in the middle of the olwm title bar, moves 300 px right over 2 s through the server's XTEST
  extension (compiled into Xzz9000 709b8a97), releases, and prints the frame's position before
  and after. Take the screenshot during the hold. It uses `XTestGrabControl`, so olwm's server
  grab during the outline move cannot stall it. The first build lacked that and deadlocked the
  server on the A4000T; if a drag hangs, kill xdrag first. Screenshot the
  result. Note where the leftover marks sit relative to the moved window: its old position, its
  new one, or the path. Repeat with a 500x400 xterm.

## Step 0b — the "slow" number (metal: A4000D, candidate-2 + Xzz9000)

```
sh ./x11perf-baseline.sh /tmp/p5-0b-1280x800.txt "p5-0b z3660 060 1280x800"
./xbench -n 3 > /tmp/p5-0b-xbench-1280x800.txt
```

Repeat both at 1920x1080 (restart with `startx-rtg 1920x1080`). The baseline takes most of an
hour: 24 tests x 3 reps x 3 s plus setup. Do not touch the mouse or keyboard during it.

The human number: time an opaque drag of a 500x400 xterm from the left edge to the right edge,
with a stopwatch or by counting frames on the capture. If olwm is in outline mode, the number
is for the outline.

Rules carried over from xrtg-amix `perf/README.md`:

- 3 repetitions. Read the spread x11perf reports; a difference smaller than the spread is no
  difference.
- The test list stays exactly as in `x11perf-baseline.sh` (it is Antti's, verbatim), so
  `x11perfcomp` can compare our files with his `perf/` series. One entry, `-text`, is not an
  x11perf option. x11perf then falls back to a substring search over the option names
  (`x11perf.c`, `Strstr(test[j].option, argv[i]+1)`), prints `-> -ftext …` for each hit, and runs
  **all sixteen `*text*` tests** (`-ftext`, `-f8text`, `-f9text`, `-f14text16`, `-f24text16`,
  `-tr10text`, `-tr24text`, `-polytext`, `-polytext16`, and the `*itext*` image-text variants). No
  NOTE is written. So `-ftext` is measured twice, and the run covers the whole text family: that
  is why a baseline takes most of an hour. The k14/k24 font tests produce no result line when the
  box lacks those fonts. The list is kept verbatim for comparability with Antti's series, which
  selects the same set.
- The header records `uname -srvm`, not `-a`. Run with `DISPLAY=:0`.
- The rig (amiberry) numbers are only for A/B between builds, never quoted as speed.

Antti notes that the X11R5 package's x11perf died on his Xrtg with BadMatch in
`HardwareSync()` (a 1x1 GetImage). If ours does the same on Xzz9000, the `-getimage*` lines say
so as `# NOTE: … failed`, and that is itself a GetImage finding worth recording.

## Fix 3 — the card blitter (RT-07)

Xzz9000 from s10rt04 on hands window fills and copies of at least `-blitmin N` pixels to the
card's blitter (the zz9000 driver's `ZZ9KIOC_BLITS`); the default, 0, never does. `blit-edge`
checks that both paths draw the same pixels. It grabs the server and covers the screen with an
override-redirect window, so run it with nothing else in progress:

```
DISPLAY=:0 ./blit-edge          # one line per FAIL, then "N cases, M FAIL -- PASS|FAIL"
```

Run it twice: on a server started with `-blitmin 1`, where every qualifying case goes to the
blitter and the X log says `zz9000: card blitter in use from 1 pixels`, and on one started
without it, which is the control. Both must PASS, 1050 cases on a 1280x800 screen. A log line
`the driver refused the card blitter` means that the kernel's driver has no blitter for this board
or mode; that run tested the CPU path only. The expected numbers were checked on a host Xvfb at
depth 16 (1050 cases PASS). A mutated copy that shifts copies and fills by one pixel fails 533 of
them.

`blit-edge -stress SECS` is the load for the metal interrupt hold-off cell. It alternates a
full-screen fill and a full-screen copy, with an XSync after each, and prints the mean
milliseconds per operation. With the blitter each operation is one stalled register write, so
with `zz9k_blit_maxarea` 0 that mean is the longest stretch without interrupts.

The rig (amiberry's Z3660_RTG stand-in) copies through a temporary buffer, so it proves the
driver's register programming and the server's record order, not the firmware's in-place
overlapping copy. That needs the metal run.

## Rebuild

```
XWORK=<copy of the Xzz9000 cross work dir> \
XBENCH=<xrtg-amix>/tools/xbench.c \
sh x11/bench/build.sh             # writes build/rt01/ + SHA256SUMS
```

`XWORK` must be a copy: the script reconfigures its X libraries as static-only. The copy of record
for the hashes above is `2026-09-27-rt01-xclients/` (kept outside the repo). A fresh copy of the Xzz9000 work
dir reproduced all four hashes exactly (2026-09-27).
