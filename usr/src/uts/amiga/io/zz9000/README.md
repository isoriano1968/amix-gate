# ZZ9000 driver for Amiga UNIX (AMIX)

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

## AMIX Installer kit

`port/make-kit-030.sh` (with `AMIX_ROOT` set to an AMIX 2.1c tree) builds the
kit the AMIX Installer links into a user's stock 68030 `/stand/unix`: the
driver for the Zorro II ZZ9000 without any target or port switch (no
`dev_kvmap`, `hat_cm_fb_add` or `monsw` needed) and with
`ZZ9K_NO_BUILTINFONT` (the stock kernel has no `builtinfont[]`),
`cdevsw[49]` registered by `port/src/patch_zz9000_cdevsw.py`, and
`zz9000init` booted through the driver chain `amixdrv_init` (amix-gate's
`port/src/amixdrv_init.s.in` and `patch_io_init.py`). The kit lands in
`port/kit` (gitignored).

`port/make-kit-040.sh` builds the 68040/68060 kit (`port/kit-040`, line=040)
for a generic-variant port kernel (`port/tools/amix-kernel-040`): the same
ZZ9000 rows, `-m68040` against the Model-B headers, with `ZZ9K_FB_NC` (the
framebuffer registered noncacheable-not-serialised through `hat_cm_fb_add`,
as rtgfb-040 does) and `ZZ9K_NO_BUILTINFONT`; no `dev_kvmap` (a Zorro II
board is in the identity map). The z3660 kernel variant has this driver built
in, so `kit-relink.sh` refuses the kit there (`zz9000init` is in the base).

## The Z3660 RTG is its own driver

The Z3660's RTG now has its own driver and kit, `../z3660rtg`
(`/dev/z3660rtg`, major 55), built from amix-z3660rtg's pristine source plus
patches that end in this directory's `zz9000.c`/`zz9000.h` and then split
the Z3660 rows off (see its README). Every change made here must be carried
there as a patch: its kit build compares the two and stops on a difference.
The `-DZZ9K_TARGET_Z3660` rows stay here for the z3660 kernel variant (major
49, as the box runs it) until that variant moves to `/dev/z3660rtg`.

## Colour modes and RTGIOC_GETCAPS

Each board row lists the colour modes its board can show; `ZZ9KIOC_SETMODE`
and `ZZ9KIOC_SETMODEHZ` return `EINVAL` for any other. The ZZ9000 shows 8,
15, 16 and 32 bit; the Z3660 8, 15 and 16 bit (its HDMI is RGB565 on a
16-pin bus: a 32-bit mode shows nothing). `RTGIOC_GETCAPS` (`../rtgcaps.h`,
copied into `zz9000.h` between the `RTGCAPS BEGIN`/`END` lines) reports the
board, its depths, framebuffer size, largest mode and `RTGCAP_*` flags, so
the X server and the CDE display panel offer only what the board can show.
