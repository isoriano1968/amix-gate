# X-Surf 100 driver for AMIX

A STREAMS/DLPI network driver for the **Individual Computers X-Surf 100**
(ASIX AX88796B, 10/100 Mbit/s, Zorro II and Zorro III) for AMIX 2.1c/2.1p2a
and the 68040/68060 port kernels: `/dev/xsf0`, character major 56,
interface `xsf0`.

**Status:** it compiles, and its kits link with 0 complaints into stock
2.1p2a (alone, with hydra, and with rtgfb and hydra) and into a 68040 port
kernel. A stock A3000/030 kernel with the kit boots in FS-UAE to the login
prompt with nothing printed (FS-UAE emulates no X-Surf). It has **not run
on an X-Surf 100 yet**: the first real-hardware test is still open.

## How it is built

`xsurf100.c` is only the X-Surf backend. The STREAMS/DLPI layer, receive
ring, transmit, overrun recovery and diagnostic ioctls are the shared
DP8390 core `../dp8390/dp8390.c`, which `xsurf100.c` includes after its
backend. The Hydra driver uses the same core, and both link into one kernel.
The backend contains:

| What | Where |
|---|---|
| AutoConfig | manufacturer 4626 (0x1212), product 100 (0x64) |
| interrupt status | board + 0x40, 16 bits, `& 0xaaaa` = network interrupt |
| AX88796B registers | board + 0x800, register *n* at + *n* * 4 |
| data port, GPOC, reset | registers 0x10, 0x17, 0x1f |
| remote DMA FIFOs | read board + 0x8880, write board + 0x8c80, 0x80 bytes each, 32-bit accesses |
| buffer RAM | 16 KB, pages 0x40-0x7f: transmit 0x40-0x45, receive ring 0x46-0x7f |
| DCR | 0x48 to read the station address PROM, 0x49 in operation |
| RCR | 0x40 + accept broadcast and multicast |
| PHY | GPOC bit 6 (power down) cleared at open, set at the last close |

The station address is read from the PROM the NE2000 way: receiver off,
loopback, 32 bytes byte-wide, each byte doubled. If that yields nothing,
the driver uses the AX88796B's PAR registers. The internal PHY sets the
MAC's duplex itself.

An A3000 has only Zorro III slots, so the card configures at 0x40000000 or
higher, outside the kernel's identity-mapped low 1 GB. The driver therefore
maps its 0x9000-byte window:

- **Stock 68030 kernel:** with `sptalloc`. The 030 line has no
  cache-inhibit page bit, so the board's CINH line is what keeps the window
  uncached.
- **68040/68060 port kernel (`-DXSURF100_KVMAP`):** with `dev_kvmap`,
  noncacheable serialised. The kit carries its own copy of `dev_kvmap`,
  named `xsurf100_kvmap`.

A board in the low 1 GB (Zorro II) is used directly.

The driver is silent when no board is present: `xsurf100init` (boot) and the
first open only ask AutoConfig.

## Kits

```sh
AMIX_ROOT=/path/to/amix-2.1c sh port/make-kit-030.sh   # -> port/kit,     line=030
AMIX_ROOT=/path/to/amix-2.1c sh port/make-kit-040.sh   # -> port/kit-040, line=040
```

`KIT.info`: `id=xsurf100`, `major=56`, `node=/dev/xsf0`, `init=xsurf100init`,
`intr=xsurf100intr`, `streams=yes`.

- `patch_xsurf100_cdevsw.py` sets `cdevsw[56].d_str` to `xsurf100info`.
- `patch_int2.py` adds `xsurf100intr` to the shared level-2 chain
  `amixdrv_int2`.
- `patch_io_init.py` adds `xsurf100init` to the boot chain `amixdrv_init`.

`port/tools/kit-relink.sh BASE OUT KIT...` links kits the way the AMIX
Installer does.

## On the machine

```sh
mknod /dev/xsf0 c 56 0
/usr/sbin/slink addaen /dev/xsf0 xsf0
/usr/sbin/ifconfig xsf0 192.168.1.100 netmask 255.255.255.0 up -trailers
/usr/sbin/route add default 192.168.1.1 1
```

## Sources of facts

- Linux `drivers/net/ethernet/8390/xsurf100.c` and `ax88796.c` (GPL),
  and iComp's X-Surf 100 register notes: facts only, no code taken.
- An untested X-Surf 100 driver for a k46 module kernel (MIT, itself a copy
  of the Hydra driver). Its addresses were cross-checked against it. Its
  code was not used: its PROM read set the remote byte count to 6 and read
  32 bytes, and it had the Hydra driver's STREAMS bugs, which the shared
  core fixes.

## Licence

MIT (see `LICENSE`), like the Hydra driver and the DP8390 core.
