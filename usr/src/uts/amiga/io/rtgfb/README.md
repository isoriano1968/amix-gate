# rtgfb: generic RTG framebuffer driver for AMIX

A small character driver, `/dev/rtgfb` (major **54**, minor 0), for graphics
cards whose mode setting and drawing run in user space, in the X server's card
module. It is the kernel side of the modular X11R6.3 server
(amix-userland `components/x11/xzz9000/docs/MODULAR-X-SERVER-EVALUATION.md`,
section 5). The driver only:

- finds the board's register and memory boards in the AutoConfig table
  (`autocon()`),
- reports what it found (`RTGFBIOC_GETINFO`, `struct rtgfb_info` in
  `port/src/rtgfb.h`),
- maps both boards for `mmap(2)`: the framebuffer at offset `0`, the
  registers at offset `0x01000000`,
- switches the monitor between the Amiga video and the card
  (`SVGAIOCSetMonitorSwitch`/`SVGAIOCGetMonitorSwitch`, the Xsvga-class RTG
  contract also used by `zz9000` and the VA2000). On the device's last close
  it switches back to the Amiga video, so an X server that dies leaves a
  usable picture.

It has no mode setting, blitter or console. It looks for its board once, at
boot (`rtgfbinit`, called by the kernel's driver chain `amixdrv_init` after
`parinit`), and names what it finds on the console, for example
`rtgfb: Picasso II (CL-GD5426), registers 0xea0000+0x10000, memory 0x200000+0x200000`
(the chip id tells a Picasso II from a II+). Without a board it prints
nothing, and the device only answers `ENXIO`.

## Boards

| Board | AutoConfig | Chip | Bus | Monitor switch |
|---|---|---|---|---|
| Village Tronic Picasso II, II+ | 2167: memory 11, registers 12 | CL-GD5426 / GD5428 | Zorro II, 1 or 2 MB | byte write to register board +0x8000 (card), +0x9000 (Amiga) |

The X card for these is `cirrus` in xzz9000's RTG core (`local-rtg-cirrus.patch`).
The Picasso II's numbers and its switch come from NetBSD's `grf_cl.c` and
`grf_clreg.h`, and from FS-UAE 3.2.35's `gfxboard.cpp`. FS-UAE emulates the same
board with `graphics_card = picasso-ii`.

## Cache class

- **Registers:** an ordinary unmanaged device page. The 68040/68060 port
  kernels map these noncacheable-serialised, which keeps VGA index/data
  writes in order.
- **Framebuffer:** written by the CPU and also by the card's BitBLT engine,
  so it must not be cached. On port kernels with the cache-class selector
  (`hat_cm_fb_add`), `relink-040-rtgfb.sh` builds with `-DRTGFB_FB_NC`. That
  registers the framebuffer as noncacheable-not-serialised, so the 68060's
  store buffer drains framebuffer writes without stalling on each one.
- **Stock 68030 kernel:** device page table entries carry no CI bit there.
  The 68030's data cache is write-through, and on the A3000/A2000 the Zorro
  II space is not cached (the board bus asserts the 030's CIIN), so the same
  mapping is correct there too.

## Building the kernel

Copy the files into your amix-040-060-port checkout (with them the shared
`src/patch_io_init.py`, `src/amixdrv_init.s.in` and `tools/kit-lib.sh` of
amix-gate's port tree):

```sh
./install.sh /path/to/amix-040-060-port
```

Then, in the port checkout, run one of the two scripts.

**A 68030 machine** (A3000 or A2500 with its own 68030, FS-UAE's A3000) runs
the stock kernel. The port kernels are 68040/68060 only. The base is the
machine's own `/stand/unix` (2.1c or 2.1p2a):

```sh
sh relink-030-rtgfb.sh /path/to/stock/unix build/unix-030-rtgfb
```

**A 68040/68060 machine** gets rtgfb as one more step of its relink chain:

```sh
sh relink-040-rtgfb.sh IN OUT
```

Each script:
- compiles `src/rtgfb.c` for its kernel (2 KB or 4 KB pages, checked in the
  object),
- links it in with `ld -r`,
- registers `cdevsw[54]` with `src/patch_rtgfb_cdevsw.py`, by symbol, which
  refuses unless the slot is `nodev`,
- hooks the boot search into `io_init[]`: `io_init` is `{ parinit, 0 }` with
  no spare slot, so the script assembles the driver chain `amixdrv_init` from
  `src/amixdrv_init.s.in` (`jsr parinit`, `jsr rtgfbinit`, `rts`) and
  `src/patch_io_init.py` retargets entry 0 from `parinit` to it. The script
  refuses unless entry 0 points at `parinit` and entry 1 is the terminating 0.
  `rtgfbinit` itself only searches for the board; it does not call `parinit`.
  On a port kernel, run `relink-040-rtgfb.sh` as the last step that touches
  `io_init`,
- checks for unresolved symbols, text/data contiguity, `.data` alignment and
  the loader's relocations.

## The AMIX Installer's kit

The AMIX Installer links rtgfb into the user's own stock `/stand/unix` on the
host (the stock kernel is Commodore's and cannot be shipped). It takes the
pieces from a kit:

```sh
AMIX_ROOT=/path/to/amix-2.1c sh port/make-kit-030.sh     # -> port/kit (gitignored)
```

The kit (version 0.2) holds `rtgfb_030.o`, `rtgfb_pad.o`,
`patch_rtgfb_cdevsw.py`, `patch_io_init.py`, `check_relink_relocs.py`,
`KIT.info` (`id`, `version`, `major`, `node`, `line`, `init`, `entries`,
`streams`) and `KIT.sha256` over all of them, in link and run order. The
installer writes one chain stub for all the drivers chosen, from
`port/src/amixdrv_init.s.in` in amix-gate's port tree, and runs
`patch_io_init.py KERNEL amixdrv_init` once, after every kit's own
`patch_*.py`. A network kit whose driver takes the level-2 interrupt also
has `intr=<symbol>` in `KIT.info` and ships `patch_int2.py`: the installer
writes one level-2 chain stub, `amixdrv_int2` (`aenintr`, then each kit's
`intr=` routine, from `port/src/amixdrv_int2.s.in`), and runs
`patch_int2.py KERNEL amixdrv_int2` once, before `patch_io_init.py`.
`port/tools/kit-relink.sh BASE OUT KIT...` does the same steps
from the shell, for tests. The same holds for the zz9000, hydra and va2000
kits (`io/<driver>/port/make-kit-030.sh`).

For the 68040/68060 port kernels (amix-gate `tools/amix-kernel-040`, generic
or z3660), `port/make-kit-040.sh` builds `port/kit-040` (`line=040`, same
version): `rtgfb_040.o` compiled `-m68040` against the Model-B headers with
`-DRTGFB_KVA` (the driver's own register accesses, monitor switch and chip
id, through a `dev_kvmap` mapping noncacheable-serialised, made once at
attach) and `-DRTGFB_FB_NC` (`hat_cm_fb_add`, which every port kernel has),
plus `devkvmap040.o` declared `provides=dev_kvmap dev_kvunmap`: the relink
leaves it out when the base kernel has `dev_kvmap` (the z3660 variant) or
when an earlier kit brought the same object (va2000-040). The 030 kit is
byte-identical with or without these changes.

## On the machine

As root, once:

```sh
mknod /dev/rtgfb c 54 0
chmod 666 /dev/rtgfb
```

## Major number

Major 54 is `nodev` in stock AMIX 2.1c and 2.1p2a and in the port kernels.
The numbers already in use are:

| Major | Driver |
|---|---|
| 46 | audio |
| 47 | Hydra |
| 48 | random |
| 49 | zz9000 |
| 50 | sad |
| 51 | zen0 |
| 53 | paula |
| 56 | X-Surf 100 (xsf0) |
| 67 | Xsvga |
| 68 | asokero's VA2000 |

Check your own `cdevsw` before you install.

## Licence

MIT, see `LICENSE`.
