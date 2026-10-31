# paula-amix: native Paula audio for AMIX

`/dev/paula` gives one program on **AMIX 2.1** (Commodore's Amiga UNIX) direct
use of Paula's four audio channels, the way a ProTracker replayer drives them
on AmigaOS. The program does not mix samples on the CPU. Instead, it queues
one batch of register changes per replayer tick, and CIA-A timer B applies
them at the song's tempo. Paula then plays the samples from chip RAM by
itself.

AMIX's own `/dev/audio` is a sample stream. A tracker on AMIX therefore had to
mix all four voices on the CPU. Measured on an A3000 with a 68060 at 80 MHz:

| Program | Through `/dev/audio` | Through `/dev/paula` |
|---|---|---|
| pt2-clone, share of the CPU spent on audio | ~41 % (float mixer), ~19 % (integer mixer) | 3–5 % |
| libmikmod beside a CPU-bound counter, work lost | ~11 % | ~5 % |
| LameCubeAMIGA42 intro with music | 28.9 fps | 30.3 fps (32.6 silent) |

The sound is Paula's own: real hardware channels, hard left/right stereo,
and the "LED" filter under program control.

## Requirements

- **AMIX 2.1 on a 68040 or 68060**, with a kernel built from
  [amix-040-060-port](https://github.com/asokero/amix-040-060-port). `/dev/paula`
  is added to such a kernel by two more steps of that repository's relink
  chain.
- A working build setup for amix-040-060-port, following its `BUILDING.md`:
  - the cross toolchain;
  - `config.sh`;
  - `AMIX_ROOT` pointing at the kernel sources of your own AMIX installation.
- A PAL machine. Paula's clock is taken as 3,546,895 Hz.

Developed and tested on an Amiga 3000 with a Z3660 (68060), booting the
port's kernel with the Z3660 drivers.

## Building the kernel

Copy the files into your amix-040-060-port checkout:

```sh
./install.sh /path/to/amix-040-060-port
```

There, the last two steps of the kernel's relink chain are the following. `IN`
is the kernel your chain has produced so far:

```sh
sh relink-040-audio-paula.sh IN unix-aud      # stock audio driver + the /dev/paula exclusion
sh relink-040-paula.sh unix-aud unix-paula    # /dev/paula itself (cdevsw[53])
```

What the two steps do:
- **`relink-040-audio-paula.sh`** rebuilds AMIX's audio driver from your
  `AMIX_ROOT` with two additions: `/dev/audio` and `/dev/paula` exclude each
  other, and `/dev/audio` refuses a second opener of its single stream.
  Commodore's source is copied and patched at build time; none of its text is
  in this repository. `src/patch_audio_paula.py` finds the four places it
  changes by fingerprint (line count and SHA-256), and stops if any of them
  does not match exactly once.
- **`relink-040-paula.sh`** compiles `src/paula.c` and links it in. It replaces
  the level-2 CIA-A handler with one that also serves timer B, and registers
  the driver as character major 53.
  - `src/patch_paula_cdevsw.py` checks the `cdevsw` layout it expects and
    refuses to patch anything else.
  - Both steps check their input and output: symbols, unresolved references,
    `.data` alignment, and 060-safe instructions.

Then finish the kernel as usual: root/swap stamp, CACR, install in `/stand`.

On the machine, once, as root:

```sh
mknod /dev/paula c 53 0 && chmod 666 /dev/paula
```

## Testing

`port/test-tools/paulatest.c` is compiled on AMIX itself with the native `cc`:

```sh
cc -O -o paulatest paulatest.c && ./paulatest
```

It checks three things:
- that `/dev/audio` is refused while `/dev/paula` is open;
- the tick rate (50 per second at 125 BPM);
- that the queue never runs dry.

It also plays a scale: a square wave on channel 0 and a saw an octave lower on
channel 1.

## Programming interface

`include/paula.h` is the complete interface (the same file as `port/src/paula.h`).

| ioctl | Argument | |
|---|---|---|
| `PAULA_SETUP` | sample area size in bytes, passed as the value | Allocates the sample area in chip RAM (up to 1.5 MB). |
| `PAULA_LOAD` | `struct paula_load *` | Copies sample data to an offset in the area. |
| `PAULA_PUSH` | `struct paula_tick *` | Queues one tick of operations. Returns `EAGAIN` when the queue (64 ticks) is full. |
| `PAULA_STATUS` | `struct paula_status *` | Ticks played, ticks that found the queue empty, ticks queued, area size. |
| `PAULA_RESET` | – | Drops the queue and stops the channels. |

A tick holds up to 48 operations, applied in order:

| Operation | Meaning |
|---|---|
| `PO_LC` | Sample start (offset in the area). `PAULA_NULLOFF` selects a silent word. |
| `PO_LEN` | Length in words. |
| `PO_PER` | Period. |
| `PO_VOL` | Volume, 0–64. |
| `PO_DMA` | DMACON bits 0–3; bit 15 set switches on, clear switches off. |
| `PO_LED` | The "LED" low-pass filter. |
| `PO_TEMPO` | BPM, 32–255: the timer B rate from the next tick on. |

Within a tick the ProTracker order is kept: DMA off, registers, a short wait,
DMA on, a short wait, then the loop pointers. A player keeps a few ticks queued
ahead and waits for `PAULA_STATUS.played` to advance. Closing the device stops
the channels and frees the area.

Only one program can hold `/dev/paula` at a time, and never while `/dev/audio`
is open. A program can fall back to `/dev/audio` when it gets `EBUSY`.

## Status

- **Tested on hardware:** the driver, in the author's kernel tree, on an
  A3000 with a Z3660 68060. `docs/REALHW-PAULA-261027.md` records the
  measurements. Players using it there are a pt2-clone port, a libmikmod
  output driver (with an automatic fallback to `/dev/audio` for modules with
  more than four channels), and ioquake3's sound output.
- **Not yet tested on hardware:** the combination in this repository. In the
  author's tree the audio driver also carries two more fixes that are not
  published here: a safe close with sound in flight, and sample-locked
  stereo. This repository builds the stock driver with only the exclusion
  added. It builds and links cleanly, and `paula.c` compiles to the same
  machine code as the tested version.

## Licence

MIT (`LICENSE`). AMIX itself is not free software, and nothing from it is in
this repository.

## Credits

- asokero, for amix-040-060-port, which this builds on.
- jusii, for the Z3660 AMIX drivers.
