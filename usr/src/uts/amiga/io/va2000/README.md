# va2000: AMIX Installer kits for the MNT VA2000

The VA2000 driver is Antti Sokero's
[va2000-amix](https://github.com/asokero/va2000-amix) (MIT). This directory
holds no copy of it: `port/fetch-source.sh` takes it pinned (commit
`7f82edcc3db9`, checked by the sha256 of `src/va2000.c` and `src/va2000.h`),
from `VA2000_SRC_DIR` (a checkout or unpacked tree) or by downloading that
commit's archive, and applies `patches/`:

- `0001-silent-without-board-zorro2-only-without-kva.patch`: no line at boot
  without a board (`VA2000_DEBUG` keeps it); without `VA2000_KVA` (the stock
  68030 kernel) a board outside the Zorro II space is named and left alone,
  because the stock kernel dereferences the AutoConfig address directly and a
  Zorro III address lies in its own virtual space.

## Kits

```sh
AMIX_ROOT=/path/to/amix-2.1c sh port/make-kit-030.sh   # -> port/kit      (line=030)
AMIX_ROOT=/path/to/amix-2.1c sh port/make-kit-040.sh   # -> port/kit-040  (line=040)
```

- **030** (stock 2.1/2.1p2a, 2 KB pages): `va2000_030.o` without
  `VA2000_KVA`, the driver's first target (Zorro II, 68030); its
  `va2000mmap` `>> 11` is the stock page shift.
- **040** (68040/68060 port kernels, 4 KB pages): `va2000_040.o` through the
  port's `src/va2000_modelb.py` (`>> 12`), the Model-B headers and
  `-DVA2000_KVA`, plus `devkvmap040.o`; Zorro II and Zorro III. The base needs
  `hat_cm_fb_add` and must not have `dev_kvmap` yet.

Both register `cdevsw[68]` with `port/src/patch_va2000_cdevsw.py` (by symbol,
asserting `nodev`) and boot `va2000init` through the driver chain
`amixdrv_init` (`port/src/amixdrv_init.s.in`, `patch_io_init.py` in amix-gate's
port tree), like every installer kit. `../../port/tools/kit-relink.sh BASE
OUT KIT...` links kits the way the installer does.

The `/dev/screen` `type=2` changes of va2000-amix's `KERNEL_CHANGES.md`
(`scrdev.c`, `c0.c`, `screen.c`, for Klaus Burkert's Xsvga) are kernel source
changes and are not part of a kit: X uses the card through `/dev/va2000`.

On the machine: `mknod /dev/va2000 c 68 0; chmod 666 /dev/va2000`.
