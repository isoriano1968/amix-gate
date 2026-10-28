# random-amix: /dev/random and /dev/urandom for AMIX

A character driver that gives **AMIX 2.1** (Commodore's Amiga UNIX) the two
devices programs expect for random numbers: `/dev/random` and
`/dev/urandom`. Stock AMIX has neither, and modern software looks for them.
With the driver in the kernel, Python's `os.urandom` and `secrets` work, as
does anything else that seeds from `/dev/urandom`.

Measured on an Amiga 3000 with a Z3660 (68060 at 80 MHz):

| | |
|---|---|
| 1 MB from `/dev/urandom` (`dd bs=4096`) | 0.35 s (`/dev/zero`: 0.28 s) |
| 256 KB through Python's `os.urandom` | 0.10 s |
| Byte distribution of 256 KB | chi-square 262 on 255 degrees of freedom; 50.03 % of bits set; no repeated 16-byte blocks |

## How it works

- **Both minors behave the same:** minor 0 is `/dev/random`, minor 1 is
  `/dev/urandom`. Neither blocks, and there is no entropy accounting.
- **The generator** is a small add-rotate-xor generator with 128 bits of
  state.
- **Seeding:** the first open seeds it from the time of day, the time since
  boot, kernel addresses and the opening process.
- **Every read** mixes in the clock again before it produces output.
- **Writing** to either device mixes the written bytes into the state, so
  any process can add seed material.
- **Seed across reboots:** `etc/randseed` feeds the seed saved at the last
  shutdown back in at boot (see below).

**This is not a vetted cryptographic generator.** It is good for seeding,
salts, temporary names, games and the many programs that just need
`/dev/urandom` to exist. Do not rely on it for long-term keys.

The driver is written for the 68040/68060 kernel. That kernel maps its data
cache-inhibited, so every memory access in a driver goes to the bus. The
generator therefore keeps its state in registers for a whole read and
stores whole 32-bit words. A first version that stirred the clock in for
every word ran at 31 KB/s.

## Requirements

- **AMIX 2.1 on a 68040 or 68060**, with a kernel built from
  [amix-040-060-port](https://github.com/asokero/amix-040-060-port). The
  driver is added to such a kernel by one more step of that repository's
  relink chain.
- A working build setup for amix-040-060-port, following its `BUILDING.md`:
  - the cross toolchain;
  - `config.sh`;
  - `AMIX_ROOT` pointing at the kernel sources of your own AMIX installation.

Developed and tested on an Amiga 3000 with a Z3660 (68060), booting the
port's kernel with the Z3660 drivers.

## Building the kernel

Copy the files into your amix-040-060-port checkout:

```sh
./install.sh /path/to/amix-040-060-port
```

There, add the driver to a kernel your chain has produced. `IN` is that
kernel:

```sh
sh relink-040-random.sh IN unix-rnd
```

The step:
- compiles `src/random.c` for the 68040 and links it in;
- checks that the base has no random driver yet and that everything the
  driver uses is there;
- registers the driver as character major 48 with
  `src/patch_random_cdevsw.py`. That script checks the `cdevsw` layout it
  expects and refuses to patch anything else.
- checks the result: symbols, unresolved references, `.data` alignment,
  060-safe instructions, and the relocation validator.

The tested kernel got the step last, after the Paula steps, the root/swap
stamp and the CACR setting. Then install the kernel in `/stand` as usual.

The port's `tools/check-verbatim.py` reports three spans in `src/random.c`:
the K&R open, close and read/write entry points, whose parameter lists the
SVR4 driver interface fixes. To record them, add these lines to
`tools/check-verbatim.allow`:

```
src/random.c ef98330ca40b	The SVR4 DDI open(9E) entry point in K&R form.
src/random.c ae30ff3a29ca	The SVR4 DDI close(9E) entry point in K&R form.
src/random.c 5fc64f25e962	The SVR4 DDI read(9E)/write(9E) entry points in K&R form.
```

## On the machine

Once, as root, if the nodes do not exist yet:

```sh
mknod /dev/random c 48 0 && mknod /dev/urandom c 48 1
chmod 666 /dev/random /dev/urandom
```

The seed script, also once:

```sh
cp etc/randseed /etc/init.d/randseed
chmod 744 /etc/init.d/randseed; chgrp sys /etc/init.d/randseed
ln /etc/init.d/randseed /etc/rc2.d/S03randseed
ln /etc/init.d/randseed /etc/rc0.d/K02randseed
```

- **At boot** it writes the saved seed, the date and the process table into
  `/dev/urandom`, then saves a fresh seed at once, so a crash never reuses
  one.
- **At shutdown** it saves the seed again, to `/etc/random-seed` (512 bytes,
  readable by root only).
- **On a kernel without the driver** it prints one "cannot create" line at
  boot and does nothing else.

## Testing

```sh
dd if=/dev/urandom bs=16 count=1 | od -x        # different every time
timex dd if=/dev/urandom of=/dev/null bs=4096 count=256
echo something > /dev/urandom                   # accepted
```

## Status

- **Tested on hardware:** the driver as it is here, as kernel
  `68060-261028-i1` on an A3000 with a Z3660 68060 (128 MB), with Paula,
  soft3D and the Hydra network card in the same kernel. The results are in
  the table above.
- **Planned:** a loadable module, so the driver can be added without
  relinking the kernel.

## Licence

MIT (`LICENSE`). AMIX itself is not free software, and nothing from it is in
this repository.

## Credits

- asokero, for amix-040-060-port, which this builds on.
- jusii, for the Z3660 AMIX drivers.
