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
