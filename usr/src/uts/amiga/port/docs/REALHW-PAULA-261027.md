# /dev/paula on real hardware — 2026-10-27

Acceptance record for the native Paula driver (`src/paula.c`, `relink-040-paula.sh`).

## Platform

**HW.** Amiga 3000, Z3660 (68060 at 80 MHz), 128 MB, booted natively (pvmk `boot` native, not
deprivileged). Kernel `a3k-kv16-ser-z3-rtg-hya-con-aud-paula-c1u0`, build id `68040-261027-i4`:
the chain `relink-040-zz9000kv.sh` (`ZZ9K_CFLAGS="-DZZ9K_FB_NC -DZZ9K_MAP_BOARD"`) →
`relink-040-hydra.sh` → `relink-040-c1hook.sh` → `relink-040-audio.sh` → `relink-040-paula.sh`,
then `tools/stamp-rootcard.py --card 1 --unit 0` and CACR `0xa0c08000`. Device node
`/dev/paula c 53 0`.

## Expected before measuring

- `open("/dev/audio")` fails with `EBUSY` while `/dev/paula` is open.
- CIA-A timer B at 125 BPM: 709379 × 2.5 / 125 = 14187 E-clock counts per tick, 50 ticks/s.
- With the queue kept full, no tick finds it empty.

## Measured (`test-tools/paulatest.c`, native `cc`)

```text
/dev/audio while /dev/paula is open: refused, EBUSY (right)
played 200 ticks in 4017 ms: 49.8 per second (expect 50.0); idle 0, area 1024
```

49.8 against 50.0 is within `gettimeofday`'s 16.7 ms step over a 4 s window. The scale the test
plays (a square on channel 0, a saw an octave lower on channel 1) was heard correctly by the
owner. Serial (IPL 5, 38400 baud) kept working during playback.

## Users measured the same day

- pt2-clone (`pt2_paula_native.c`, sdl2-amix null audio): playback through `/dev/paula` judged
  correct by ear; the program's audio share fell from ~41 % (float mixer) / ~19 % (integer
  mixer) to 3–5 %.
- libmikmod `drv_paula`: a 10 s CPU-bound counter lost ~5 % next to mikmod on `drv_paula`
  against ~11 % next to mikmod mixing on `drv_amix`. LameCubeAMIGA42 with music: 28.9 → 30.3 fps
  (32.6 silent).

## Side finding

AMIX charges system time to a process sleeping in `poll()` with a timeout (20 × `poll(NULL, 0,
100)`: 97 of 140 elapsed ticks as `sys`), but a CPU-bound counter beside it is unaffected (67 925
loops alone, 66 854 next to `poll`, 67 292 next to `sleep`). `ps` TIME of a program that waits in
`poll`/`select` therefore overstates its CPU use; measure with a work counter instead.

## Not covered

- `/dev/paula` was not exercised on a 68040 or a 68030 machine.
- The DMA-off → on and on → loop waits are 5 scanlines each; no module with very low periods was
  tested for clicks.
- `tools/check-verbatim.py` could not run on this machine (no reference trees, exit 2).
