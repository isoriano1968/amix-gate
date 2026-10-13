# ZZ9000 driver for Amiga UNIX (AMIX)

## This fork: the Z3660 RTG target (amix-z3660rtg)

This is a fork of [isoriano1968/zz9000-amix](https://github.com/isoriano1968/zz9000-amix)
(MIT, Ignacio Soriano; `LICENSE` is upstream's, unchanged; see `NOTICE` for this fork's
additions). It adds the Z3660 accelerator's onboard RTG as a second target. The registers stay
ZZ9000-compatible, `/dev/zz9000` stays at major 49 and the ioctl ABI does not change. The Z3660
identity and windows are behind one switch, `-DZZ9K_TARGET_Z3660` (see `DESIGN.md`). The
default build is upstream's ZZ9000 driver. The text after this section is upstream's README,
left unchanged.

- **Status: initial working version, metal-proven end to end.** A 68LC060 kernel with this
  driver and the cross-built X11R6.3 `Xzz9000` bring the OpenLook desktop up on the Z3660 HDMI
  of a real A4000 (`docs/DEMO-RTG.md`, first run 2026-09-19). The release kit of record is
  pinned in `x11/KIT.sha256`: the server with the card blitter, the keyboard hand-back tool,
  `startx-rtg` and the session file.
- **Next: performance.** Each card blitter operation costs about 76 µs of fixed overhead in the
  firmware path (`NOTES.md`). Whether the ZZ9000 and Z3660 targets stay one source is still
  open.
- **Known limitation:** user `mmap` of `/dev/mem` is broken on the r13 kernel lineage (the
  68040/68060 port's ISSUE-46: every mapping lands one 4 KiB page high). It does not affect
  `/dev/zz9000`, because `zz9000mmap` is a different `d_mmap`. Do not use `/dev/mem` mmap as
  an oracle on those kernels.

### Getting started

- **Driver.** `install-source.sh` and `INSTALL.md` (upstream's procedure) put the driver into
  an AMIX kernel source tree. For the Z3660 build:
  `make ZZ9K_TARGET=-DZZ9K_TARGET_Z3660` (the Makefile header lists the bench and console
  options).
- **Kernel.** A Z3660 runs a 68040/68060 kernel from Antti Sokero's
  [amix-040-060-port](https://github.com/asokero/amix-040-060-port). The kernels here were
  assembled with [amix-kerntools](https://github.com/jusii/amix-kerntools), which builds this
  driver through its `make=ZZ9K_TARGET=…` record (`DESIGN.md` §4.2).
- **X server.** `docs/X-CROSS.md` cross-builds isoriano's
  [x11r6.3-amix](https://github.com/isoriano1968/x11r6.3-amix) `Xzz9000` with
  [gcc-cross-amix](https://github.com/jusii/gcc-cross-amix) and the patches in `x11/patches/`.
  It needs an AMIX sysroot copied from an installed system. `docs/X-INSTALL-RTG.md` puts the
  result on a disk image; `x11/README.md` lists the kit.
- **Bench (emulator).** The ZZ9000 rows run on stock
  [amiberry](https://github.com/BlitterStudio/amiberry)'s ZZ9000 emulation. Stock amiberry takes
  the display pitch from the blitter X2 register on every pan write, so the screen stays black
  after the driver switches to RTG. Run `bench/zzpanfix.c` (build on the box:
  `cc -I overlay/usr/sys/amiga/driver/zz9000 -o zzpanfix bench/zzpanfix.c`, then
  `zzpanfix /dev/zz9000 <width>`). The **Z3660 bench** (the Z3660-RTG stand-in) and the pan fix
  live in our amiberry fork (github.com/jusii/amiberry, branch `amix-v830`), which is not public
  yet. Until it is, the Z3660 target is verified on metal only.
- **Background.** [grimoire-amix](https://github.com/jusii/grimoire-amix) documents the AMIX
  driver model (`docs/drivers/driver-model.md`) and the ZZ9000 and RTG X servers
  (`docs/drivers/case-studies/zz9000.md`, `x11-rtg-drivers.md`).
- **Docs here:** `DESIGN.md` (the switch and board table), `NOTES.md` (running log),
  `CHANGELOG.md`, `docs/DEMO-RTG.md` (the metal record), `docs/FIRMWARE-RTG-INTERFACE.md` (the
  Z3660 firmware's RTG interface), `docs/BENCH-*.md` (bench recipes), `x11/bench/RUN.md` (the X
  measurement kit).

### Constraints for changes

The driver is compiled natively by the AMIX box's 1992 `cc`. If upstream code needs GCC-only
constructs, report it rather than rewriting it silently. Keep upstream's driver bytes verbatim
where unchanged; every Z3660 difference lives behind the one switch and is documented for the
upstream author.

### Glossary of work-item names

The log, the patches and some source comments name work items:

- **P-5** — the X server correctness and speed work. It covers suspects **S1–S5**, server bugs
  checked by the discriminator clients in `x11/bench/`, and fixes 1–3. Fix 3 is the card
  blitter.
- **P-7, P-8** — the RTG monitor switch: P-7 is the interim userland pair, P-8 the server hook.
- **RT-01 … RT-12** — the RTG work items (`CHANGELOG.md`, `NOTES.md`).
- **M1, M1b, M2** — blitter cost measurements: `zz9k_blitbench` on the card, then the
  firmware-side profile.
- **MGn** — numbered metal test runs on a real A4000.
- **K1, KT-nn, PO-nn** — items of the install-media builder, amix-kerntools and the 68040/68060
  port.
- **ISSUE-nn** — entries in the 68040/68060 port's `KNOWN-ISSUES.md` (Antti Sokero; public).

**Out-of-repo citations.** Some records in `NOTES.md` and `docs/` cite evidence directories,
sibling repositories or archived images by name. They are not part of this repo and are not
needed to build or use the driver.

---


This repository contains the AMIX kernel framebuffer driver for the MNT
ZZ9000, its user-space control and diagnostic tools, and optional late-boot
console redirection.

The driver is the hardware foundation used by the ZZ9000 X11R5 and X11R6.3
RTG servers. It provides mode setting, framebuffer mapping, RGB565/32-bit
framebuffer access, panning, display switching, fill/copy blits, and a color
framebuffer console.

## Hardware scope

- Amiga 3000UX running AMIX
- ZZ9000 configured as **Zorro II product 3**
- MNT manufacturer ID `0x6d6e`
- Character-device major `49`, minor `0`
- Tested primary mode: `1920x800`, RGB565

AMIX has no Zorro III bus support. ZZ9000 Zorro III product 4 is detected by
the diagnostic tool but is intentionally rejected by the kernel driver. USB
is not implemented. The current release covers graphics and console services;
it does not include a ZZ9000 Ethernet driver.

## Repository layout

- `overlay/usr/sys/amiga/driver/zz9000`: kernel driver and tools
- `integration`: exact AMIX source-tree integration fragments
- `install-source.sh`: installs the driver subtree into an AMIX source tree
- `INSTALL.md`: build, kernel, device-node, console, and validation procedure

## Safety model

The driver attaches during normal kernel initialization, but it does not take
over the boot console. Earlier experiments that enabled RTG console output
before the native AMIX console was initialized could stop the machine before
filesystems or networking became available. Console redirection is therefore
performed from `/etc/rc2.d/S99zz9000`, after the system is operational.

For X11 use, the optional console hook and init script are not required. The X
server communicates directly with `/dev/zz9000`.

See [INSTALL.md](INSTALL.md) for the complete procedure.
