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
boot, and names what it finds on the console, for example
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

Copy the files into your amix-040-060-port checkout:

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
- hooks the boot search into `io_init[]` with `src/patch_rtgfb_init.py`:
  `io_init` is `{ parinit, 0 }` with no spare slot, so entry 0 is retargeted
  from `parinit` to `rtgfbinit`, which calls `parinit` first. The script
  refuses unless entry 0 points at `parinit` and entry 1 is the terminating 0,
- checks for unresolved symbols, text/data contiguity, `.data` alignment and
  the loader's relocations.

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
| 67 | Xsvga |
| 68 | asokero's VA2000 |

Check your own `cdevsw` before you install.

## Licence

MIT, see `LICENSE`.
