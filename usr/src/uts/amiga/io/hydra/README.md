# AMIX Hydra Ethernet Driver

A STREAMS/DLPI network driver for the **Hydra Systems AmigaNet rev 1.2a** Zorro II Ethernet card (National Semiconductor DP8390/NE2000) for **AMIX 2.1p2**, Commodore's System V Release 4 Unix for the Amiga.

## Current Status

The driver has now been verified on real Hydra AmigaNet hardware under AMIX:

- The interface can be plumbed and configured.
- ARP resolution works.
- ICMP ping works to local gateway addresses.
- ICMP ping works to external IP addresses such as `8.8.8.8`.
- The card's RX/TX LEDs show live traffic during operation.

This is believed to be the first working AMIX network driver for the Hydra
Systems AmigaNet card.

The key fix that made ARP and ping work was the RX minimum-frame check. The
DP8390 receive ring count includes the 4-byte Ethernet CRC. After the driver
subtracts the CRC, a minimum Ethernet frame is 60 bytes, not 64 bytes. The
driver now validates post-CRC receive frames against `ETH_MINFRAME`:

```c
#define ETH_MINFRAME (ETH_MINPACKET - ETH_CRC_LEN)
```

## Background

AMIX (Amiga Unix) is SVR4 Unix for Motorola 68020+ Amiga systems, originally developed by Commodore-Amiga and later sold by Haage & Partner. It includes a fully-functional STREAMS networking stack with DLPI-based Ethernet drivers.

The existing AMIX kernel ships with the `aen` driver for the Commodore A2065 Ethernet card (AMD LANCE-based). This project adds support for a second NIC: the **Hydra Systems AmigaNet**, a Zorro II card built around the ubiquitous **National Semiconductor DP8390 (NE2000)** chip.

The driver follows the same DLPI/STREAMS architecture as the A2065 driver, making it a useful reference for anyone porting other Zorro II network cards to AMIX.

## Hardware

| Feature | Detail |
|---------|--------|
| Card | Hydra Systems AmigaNet rev 1.2a |
| Chipset | National Semiconductor DP8390 (NE2000-compatible) |
| Bus | Zorro II (autoconfig ID: 2121/1 = 0x08490001) |
| NIC offset | board + 0xffe1 (odd byte lane, D0-D7) |
| MAC PROM | board + 0xffc0 (16-bit bus, every other byte) |
| SRAM | 16 KB on-card packet buffer |
| Media | 10Base2 (BNC) and 10BaseT (RJ45) |

## Repository Structure

```
README.md
stand/                  Boot code (level 1 + level 2 bootstrap, COFF/ELF tools)
usr/
  sys/
    amiga/
      boot/             boot2.c bootstrap loader
      config/           kernel config (unix.c, mapfiles)
      driver/
        aen/            A2065 LANCE driver (reference implementation)
        hydra/          This driver (ne2000.h, hydra.c, hydra.h, ...)
          hya/          hya user-space control and diagnostic tools
      kernel/
        servant.s       halt/reboot (ASM)
        support.c       autocon() / bootinfo handling
      Makefile          top-level platform build
    master.d/
      kernel            SVR4 master.d config
      kernel.c          cdevsw, int2_tbl, init_tbl
    ml/                 machine layer (3B2 derived)
    os/                 OS core
    Makefile            top-level kernel build
```

## Driver Architecture

The driver is a STREAMS DLPI provider (`struct streamtab hydrainfo`) registered at cdevsw slot 47, using major/minor device `hya`. It follows the same pattern as the A2065 (`aen`) driver:

- **`hydraopen()`** - STREAMS open; calls `hydraautoconfig()` to detect the card, then `hydra_initialize()` to set it up
- **`hydraautoconfig()`** - three-method card detection (runs once, cached):
  1. `autocon(NE8390_BOARD_ID, ...)` - uses kernel's bootinfo autoconfig table with Zorro II address validation
  2. Direct Zorro II I/O slot probe - reads MAC PROM at each 64 KB slot (0xE90000-0xEFFFFF)
  3. Memory space probe - scans Zorro II memory space (0x200000-0x9FFFFF) at 64 KB steps
- **`hydrawput()`** - STREAMS write-side put procedure, handles DLPI primitives (DL_INFO_REQ, DL_BIND_REQ, DL_UNITDATA_REQ, etc.)
- **`hydraintr()`** - INT2 interrupt handler, registered in `int2_tbl[]`, processes RX/TX/error interrupts from the DP8390
- **`setup_ne2000()`** - initializes the DP8390 registers, ring buffers, and physical address

## Building

### Prerequisites

- **AMIX 2.1p2** installed on an Amiga (A2500/A3000 (support of AMIX on A4000 via z3660(030emu) is currently in the works)  with development system
- **Native GCC 2.7.2.3** for AMIX (not included; download from [amigaunix.com](https://amigaunix.com) as pkg format)

### Kernel build

The driver is compiled natively on the Amiga under AMIX. No cross-compiler is required.

```sh
su
cd /usr/sys/amiga/driver/hydra
make
```

This produces a relocatable object file `exp` that can be linked into a custom kernel.

To rebuild the full kernel with the hydra driver:

```sh
cd /usr/sys
make force
```

The `makedev` tool is used to install the device node:

```sh
mknod /dev/hya0 c 47 0
```

## Testing

### Manual interface setup

AMIX does not support `ifconfig plumb` (SVR4.0 predates that mechanism). The interface is plumbed via `slink`:

```sh
su
/usr/sbin/slink addaen /dev/hya0 hya0
/usr/sbin/ifconfig hya0 192.168.1.100 netmask 255.255.255.0 up -trailers
/usr/sbin/route add default 192.168.1.1 1
ping 192.168.1.1
```

The `route add default ... 1` suffix is the metric (required on AMIX).

With the current driver sources, ping has been verified against both local
gateway addresses and external IP addresses.

### Boot-time configuration

Add to `/etc/inet/network-config` (sourced by `/etc/rc2.d/S69inet`):

```sh
/usr/amiga/bin/hya -S || exit 0
/usr/sbin/slink addaen /dev/hya0 hya0 2>/dev/null
/usr/sbin/ifconfig hya0 192.168.1.100 netmask 255.255.255.0 up -trailers
```

The `hya -S` line silently exits the script if no Hydra board is detected, preventing the boot script from hanging on `slink`.

## hya Control Utility

The `hya` tool (in `usr/sys/amiga/driver/hydra/hya/`) is a user-space utility for checking Hydra board presence and reading configuration. It is modeled after the `aen` tool for the A2065 card.

The `hydra_test` diagnostic tool in the same directory can dump driver
counters, status fields, and passive RX ring maps. It avoids raw access to
dangerous Hydra register slots that can trap the AMIX kernel.

### Building

```sh
cd /usr/sys/amiga/driver/hydra/hya
make
make install          # installs to /usr/amiga/bin/hya
```

### Usage

| Flag | Description |
|------|-------------|
| `-S` | Silent check; exit 0 if board found, 1 if not (for boot scripts) |
| `-n` | Print number of Hydra boards detected |
| `-c` | Show MAC address, board base, and debug level |
| `-d dev` | Use alternate device (default `/dev/hya0`) |

### Boot script integration

The `hya -S` command is used to conditionally plumb the interface:

```sh
/usr/amiga/bin/hya -S && /usr/sbin/slink addaen /dev/hya0 hya0 && \
    /usr/sbin/ifconfig hya0 192.168.1.100 netmask 255.255.255.0 up -trailers
```

## Known Issues

- **autocon() table corruption**: The kernel's `autocon()` function reads from the `bootinfo` ConfigDev table populated by `config()` in `support.c`. On AMIX 2.1p2 the table entries can be corrupted (address/size mismatches), so the driver validates addresses and falls back to direct slot/memory probes.
- **PROM byte lane**: The MAC PROM at board+0xFFC0 uses 16-bit bus with step-2 byte access (every other byte). Direct reads without step-2 return garbage.
- **`ifconfig plumb` does not exist on AMIX**: Use `slink addaen /dev/hya0 hya0` instead. The interface name matches the device minor (`hya0` for minor 0).
- **Remote DMA hang**: The standard NE2000 byte-at-data-port RDMA method does not raise RDC on the Hydra card. The AmigaOS reference driver uses Hydra-specific ASIC registers (`HYDRA_LOAD1`/`HYDRA_LOAD2` at board+0x8000+0x7FD2/0x7FD5) and writes packet data directly to the card's buffer RAM at the board base address.
- **Register 6 / TBCR1 / FIFO traps**: Raw diagnostic reads or writes around DP8390 register 6 can trap the AMIX kernel on Hydra hardware. The diagnostic tool avoids these accesses.

## Resolved Issues

- **Minimum-size ARP replies were dropped**: The RX ring reports minimum frames
  as 64 bytes including CRC. After CRC removal they are 60 bytes. The driver
  now checks against `ETH_MINFRAME`, so valid ARP replies are accepted and ping
  proceeds beyond ARP.

## License

AMIX is proprietary software of Commodore-Amiga / Haage & Partner. This driver is provided as a reference implementation for educational and research purposes. No AMIX kernel binaries are included in this repository; only build scripts and driver source code intended to be compiled against a licensed AMIX 2.1p2 installation.

## References

- *AMIX System Administrator's Guide* (Commodore-Amiga)
- *DP8390/NS32490 NIC Datasheet* (National Semiconductor)
- *NE2000 Programmer's Guide* (Novell/National)
- *Amiga Hardware Reference Manual* (Commodore)
- *System V Release 4 STREAMS Programmer's Guide* (AT&T)
- [Hydra AmigaNet reverse-engineering (Ville Jouppi)](https://github.com/vjouppi/hydra) - AmigaOS driver, register offsets, board-level schematics
