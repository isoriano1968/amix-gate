# zz9000eth: AMIX driver for the MNT ZZ9000's Ethernet port

A STREAMS/DLPI network driver for the Ethernet port of the **MNT ZZ9000**
(Zorro II or Zorro III) under AMIX. It is `/dev/zz0` (character major 52)
and the interface `zz0`. AMIX never had one before.

**Status: not yet run on a real ZZ9000.** It links into stock AMIX 2.1p2a
and 2.1 and into the 68040/68060 port kernels, and it boots to `login:` in
FS-UAE A3000/030. FS-UAE does not emulate a ZZ9000, so that boot only shows
that the driver is silent and harmless without the board. See "What only a
real ZZ9000 can test" below.

## Where it comes from

The driver is derived from **z3660eth** by Jussi Alanärä (`../z3660net`,
MIT), the driver for the Z3660 accelerator's Ethernet. The Z3660 firmware
descends from the ZZ9000 firmware, so the protocol is the same: the board's
ARM firmware owns the Ethernet controller (Zynq GEM) and passes whole frames
through a small set of registers and shared frame windows. Everything above
the hardware is z3660eth's, renamed: STREAMS open and close, DLPI, the
receive demultiplexer, receive backpressure, the poll callout and the
040/060 cache gate. About 89% of the source lines are z3660eth's as they
were. The board layer is new.

## The ZZ9000 interface

These facts come from MNT Research's ZZ9000 firmware (`ZZ9000OS/src`:
`zz_regs.h`, `main.c`, `ethernet.c`, `memorymap.h`, and `mntzorro.v`) and
from its AmigaOS `ZZ9000Net.device` (`net/device.c`). Both are
GPL-3.0-or-later. **No code was taken from them.**

| Offset | Access | Meaning |
|--------|--------|---------|
| `0x04` | W | `1` turns the Ethernet interrupt on, `0` turns it off, `24` (8\|16) acknowledges it |
| `0x04` | R | pending Amiga interrupt bits: Ethernet = 1, audio = 2 |
| `0x80` | W | frame length: sends the frame at `+0x8000`, synchronously, in up to about 1 ms |
| `0x80` | R | result << 8: 0 ok, 1 no link, 2/3 descriptor error, 4 timeout |
| `0x82` | W | the frame in the RX window has been consumed |
| `0x84`, `0x86`, `0x88` | R/W | MAC bytes 0–1, 2–3, 4–5; writing `0x88` sets the address |
| `0x2000`–`0x5FFF` | R | RX window: the firmware's current backlog slot, as `[length][serial]` (2 + 2 bytes) followed by the frame |
| `0x8000`–`0x9FFF` | W | TX frame |

- The registers are 16 bits wide. On Zorro II a register read at an offset
  with A1 = 0 returns bits 31..16 of the firmware's word, and with A1 = 1 bits
  15..0.
- The receive backlog has 32 slots. When it is full, new frames are dropped.
- The firmware raises **INT6** while frames are waiting, but only if the
  interrupt is turned on in `0x04`.
- The firmware's default MAC address is `68:82:F2:00:01:00`. AmigaOS sets
  another one from `ENV:ZZ9K_MAC`.

### How it differs from z3660eth

The Z3660 kept the protocol but moved things around:

- The Z3660 has 32-bit registers at `0x104`..`0x1A8` and DDR frame windows at
  `+0x07ED0000`. It also has a receive cursor register (RX_ADDR) and a 64-slot
  overrun range.
- On the ZZ9000, the RX window shows only the current slot. So the driver
  bounds the time it holds a frame back when a stream is full, but not the
  number of slots.
- When the driver has consumed every waiting frame, the ZZ9000 firmware resets
  its backlog. Slot 0 then shows the first frame of the round it just
  consumed, which has an **older** serial. The Z3660 firmware rewrites that
  serial; the ZZ9000 firmware does not. So zz9000eth takes a frame only when
  its serial is ahead of the last one consumed.
- On its first open, the driver takes the serial in the window as already
  seen and consumes that slot. It drops at most one stale frame and never
  delivers one.
- The Z3660 needs every register access issued behind an "arming" read
  because of its FPGA's ZS-05 address-capture defect. The ZZ9000 does not, so
  its accesses are plain 16-bit ones.
- The transmit result reads back byte-swapped.

## Detection, sharing the board, interrupts

- **AutoConfig only:** MNT (`0x6d6e`) product 3 (Zorro II), then product 4
  (Zorro III). The driver never reads a raw address. It looks for the board at
  the first open of `/dev/zz0`, not at boot, so a kernel with the driver
  touches nothing and prints nothing without a ZZ9000. A board whose Ethernet
  does not answer gets one console line that names it: `zz9000eth: ZZ9000
  Zorro II: ethernet does not answer ...`.
- **Sharing with the graphics driver.** `../zz9000` (`/dev/zz9000`, major 49)
  drives the same card. zz9000eth uses only registers `0x04` and `0x80`–`0x88`
  and the windows `0x2000`–`0x9FFF`. The graphics driver uses none of these,
  and zz9000eth maps neither the framebuffer nor the rest of the register
  page. Neither driver takes an interrupt. The two kits link together.
- **Interrupts.** The ZZ9000 signals frames on level 6. AMIX has no level-6
  driver chain, and an enabled but unacknowledged INT6 storms (z3660eth found
  this on real hardware). So the driver keeps the interrupt off (it writes
  `0x04` = 0) and drains the backlog from a one-tick `timeout()` poll, as
  z3660eth does by default. `zz9000ethintr()` is ready for a later chain. For
  now `KIT.info` has `intr=` empty, because the int2 chain
  (`port/src/patch_int2.py`) serves level 2 and the ZZ9000 interrupts on
  level 6.

## Mapping and caches

- **Zorro II board** (`0x200000`–`0x9FFFFF`): reached through AMIX's
  identity map of the low 1 GB. No mapping call is needed and no kernel VA is
  used.
- **68030 line:** the data cache is effectively off, which is z3660eth's
  proven no-flush setup.
- **68040/68060 line:** `DTT0` makes the window cache-inhibited. The cache
  gate and line operations are z3660eth's and are built only into the 040
  kit.
- **Zorro III board:** mapped noncacheable through the port's `dev_kvmap` on
  the 040 line, and through `sptalloc()` on the stock kernel. Only the
  register page and `0x2000`–`0x9FFF` are mapped.

## Kits (AMIX Installer)

```
sh port/make-kit-030.sh    # kit/      line=030, stock 2.1/2.1p2a kernels
sh port/make-kit-040.sh    # kit-040/  line=040, 68040/68060 port kernels
```

Both need `AMIX_ROOT` and `m68k-cbm-sysv4-gcc` (gcc 2.7.2.3) on `PATH`, as
the other kits do.

- `KIT.info`: `id=zz9000eth`, `major=52`, `node=/dev/zz0`, `init=` (lazy
  open), `intr=` (none), `entries=zz9000ethinfo` (plus the private
  `zz9000eth_kvmap`/`zz9000eth_kvunmap` in the 040 kit), `streams=yes`.
- `patch_zz9000eth_cdevsw.py` moves the `d_xhalt` relocation of slot 52 to
  `d_str`, the same mechanism as hydra's.
- Major 52 is `nodev` in 2.1, 2.1p2a and the port kernels. It is not claimed
  by any AMIX driver this tree knows: 46 noise, 47 hydra, 48 random, 49 zz9000,
  50 sad, 51 z3660eth, 53 paula, 54 rtgfb, 56 xsurf100, 67 svga, 68 va2000.

Network setup, as the installer writes it for the HydraNet:

```
mknod /dev/zz0 c 52 0
/usr/sbin/slink addaen /dev/zz0 zz0 2>/dev/null &&
	/usr/sbin/ifconfig zz0 `uname -n` up -trailers
```

## What only a real ZZ9000 can test

Everything about the datapath:

- that the MAC address reads back correctly;
- that transmit results decode as above;
- that a frame round-trips, both ARP and ping;
- whether the received length includes the FCS (z3660net's
  ETHERNET-SCOPING Q4);
- the serial rule at catch-up;
- receive under load (tail drops, `zz9000eth_rx_stale`/`_rx_skipped` in
  `/dev/kmem`);
- Zorro III mode on either line;
- the 68040/68060 cache arm on real silicon;
- coexistence with X on the graphics driver.

## Licence

MIT, see `LICENSE`. The copyright is Jussi Alanärä's for z3660eth and ours
for the changes. `NOTICE` carries the trademark and third-party notes.
