# Fault knob: one driver-side WRITE fault for the DI-1 rig cells

**Diagnostic cuts only — never in a shipping chain.** An object built from this source carries
the knob, inert while `z3660_fault_lba` is 0. A shipping chain pins an object built without it,
like the shipping KD060-Z object, or reverts the commit that added it before cutting a release.

## Why

The emulated Z3660 mailbox is synchronous and cannot fail a transfer, so a rig run never reaches
the kernel's own error handling: dd.c's `sdintr` retry and the completion-order shims. kerntools
found this while preparing the DI-1 cells. The knob lets the host or an in-guest `adb -w` make
**one** chosen WRITE fail, or land at the wrong LBA, from the driver side. The kernel's reaction
can then be watched on the rig.

## What it does

The check sits in `z3660queue()`, after every existing guard (CD-ROM write reject, range,
geometry, ZS-04) and just before `z3660_rw()`. It fires when all three hold:
`z3660_fault_lba != 0`, the CDB is a WRITE(6)/WRITE(10), and the request's **starting** block
equals `z3660_fault_lba`. When it fires, the driver:

1. clears `z3660_fault_lba` to 0 (one-shot) and increments `z3660_fault_n`;
2. **mode 0, REFUSE** (also any mode other than 1): it completes the request as a `z3660map()`
   failure does, with `status` 0xff and `okay` FALSE. Nothing is transferred and no doorbell
   rings. The completion is delivered outside the mailbox bracket by `z3660_complete()`. In the
   040 object this arm branches to the same instructions as the ZS-04 and range refusals
   (`z3660queue+0x55a`);
3. **mode 1, SHIFT**: it runs the WRITE through the normal `z3660_rw()`, but with the block
   register set to `block + z3660_fault_shift`, and reports GOOD. The default shift is +4: the
   payload lands 4 blocks (2 KiB on a 512-byte disk) higher. If the shifted range would start
   below LBA 0 or pass the unit's end, it REFUSEs instead.

What stays untouched: reads, writes that start at any other LBA (also one that only *covers*
the armed LBA), CD-ROM writes (rejected earlier with DATA PROTECT), every request an earlier
guard refuses, and everything when `z3660_fault_lba` is 0. In the off state the cost is one
`tstl` and a taken `beq` per block READ/WRITE. Only `z3660queue()` changed in code: every
other function, `z3660_rw()` included, is instruction-identical to bd8d900's objects. Some
differ only in `.data` relocation addends, because the knob takes 16 bytes of `.data` right
after the ring.

There is no unit filter. The first WRITE on **any** unit that starts at the armed LBA fires it.
The LBA is the unit-absolute block in the CDB: what the mailbox gets in `WRITE_ADDR1` and what
the D245 write capture logs. It is not partition-relative.

## Symbols

All four are explicitly initialized, so gcc 2.7.2.3 puts them in `.data`, not COMMON (`nm`:
`D`). Each has a fixed offset in the object and file bytes in the kernel image. Unlike the
COMMON counters, their link address does not depend on the COMMON allocation.

| symbol | type | object `.data` offset | init | meaning |
|---|---|---|---|---|
| `z3660_pagecross_ring` | `D` | +0x0F0 (516 B) | "ZPCR" + 0 | base check, see below |
| `z3660_fault_lba` | `D` ulong | +0x2F4 = ring + 0x204 | 0 | 0 = off; else the WRITE start LBA to fault |
| `z3660_fault_mode` | `D` ulong | +0x2F8 = ring + 0x208 | 0 | 0 REFUSE, 1 SHIFT, other = REFUSE |
| `z3660_fault_n` | `D` ulong | +0x2FC = ring + 0x20C | 0 | times fired, either mode |
| `z3660_fault_shift` | `D` long | +0x300 = ring + 0x210 | 4 | SHIFT offset in blocks, signed |

The offsets are measured on both `z3660_040.o` and `z3660_030.o` from `build/mkobj.sh fk`.
In that relocatable relink (driver-line base, `ld -r`) they sit at `.data`+0x1a54c / 0x1a550 /
0x1a554 / 0x1a558, with the ring at `.data`+0x1a348. That is not a rig kernel. Take the
runtime addresses from the kernel you actually boot.

## Arming

**Resolve.** Get the runtime addresses of the four knobs and of `z3660_pagecross_ring` the same
way as for the other counters: from the chain's post-COMMON map, or with kerntools'
`tools/comaddr.py <kernel ELF> <symbols> --load-base 0x08000000 --anchor fpe_magic --observed
<word>`, as `kread2.py` does. Runtime address = load base + combined offset. On the KD line the
base is 0x08000000. Verify it, never assume it.

**Base check.** The 4 bytes at `z3660_pagecross_ring` must read 0x5A504352 ("ZPCR";
`1515209554` in the IPC's decimal). `z3660_fault_lba` must resolve to exactly ring + 0x204.
Before arming, the knobs read lba 0, mode 0, n 0 (or the count so far), shift 4.

### From the host: amiberry IPC

amiberry's IPC socket (`<slot>/run/amiberry.sock` on the rig) takes tab-separated lines:
`READ_MEM <addr> 4` answers `OK <decimal>`, and `WRITE_MEM <addr> 4 <value>` answers `OK`.
The value goes through `stoul(…, 0)`, so `0x` hex works. Write a negative shift as its 32-bit
pattern, for example `0xFFFFFFFC` for −4.

```python
import socket
def ipc(sock, line):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.settimeout(5); s.connect(sock)
    s.sendall((line + "\n").encode()); r = s.recv(256).decode().strip().split("\t"); s.close()
    return r

assert ipc(S, "READ_MEM\t0x%x\t4" % RING) == ["OK", "1515209554"]   # "ZPCR"
assert LBA == RING + 0x204
ipc(S, "WRITE_MEM\t0x%x\t4\t%d" % (MODE, 0))           # 0 REFUSE / 1 SHIFT
ipc(S, "WRITE_MEM\t0x%x\t4\t0x%x" % (SHIFT, 0xFFFFFFFC))  # only for a non-default shift
ipc(S, "WRITE_MEM\t0x%x\t4\t%d" % (LBA, lba))          # LAST: a non-zero LBA arms it
print(ipc(S, "READ_MEM\t0x%x\t4" % LBA))               # read back
# ... run the cell ...
print(ipc(S, "READ_MEM\t0x%x\t4" % N), ipc(S, "READ_MEM\t0x%x\t4" % LBA))  # n+1, 0
```

Write the mode and the shift before the LBA. The driver reads all of them only after it sees a
non-zero LBA, inside its `spl6` bracket. `WRITE_MEM` writes through amiberry's memory banks, not
through an emulated CPU cache. The rig configs run with `cpu_data_cache=false` (kerntools
`rig/configs/E1.uae`), so the guest's next load sees the poke. On a config with data-cache
emulation turned on that no longer holds, so read back, or arm from inside the guest.

### From inside the guest: `adb -w` and kmemdump

Address by runtime VA with **no namelist**. The namelist trap is grimoire's "Live-probing a
running kernel with adb on /dev/kmem". The kernel's `.data` lies in the identity-mapped region
below 0x40000000, so `/dev/mem` at the same number reaches the same byte if `/dev/kmem` is in
doubt. In adb, `0t` means decimal.

```sh
echo '0x<MODE>/W 0'       | adb -w - /dev/kmem     # or 1 for SHIFT
echo '0x<LBA>/W 0t34312'  | adb -w - /dev/kmem     # LAST: arms it
kmemdump <LBA-hex> 16                              # lba mode n shift, as longs
```

`kmemdump` is amix-a4091's `src/kmemdump.c`. It only reads. Mind the no-unit-filter rule: a
write by the arming session itself can fire the knob if it starts at the same LBA.

**Fired?** `z3660_fault_n` went up by exactly 1 and `z3660_fault_lba` reads 0. If the LBA still
reads non-zero after the cell, no WRITE started there: wrong LBA, partition-relative instead of
unit-absolute, or the request was split differently.

## Which picture each setting gives, for the DI-1 8 KiB inode-block write

The kernel sends an 8 KiB metadata write as four 2 KiB WRITEs, in order, at L, L+4, L+8 and
L+12 (DI-1-misplaced-write.md §8.1). The table is derived; the SHIFT −4 row is also a host
cell (below).

| armed at | mode / shift | on disk afterwards | kernel sees |
|---|---|---|---|
| L+4 (2nd piece) | REFUSE | if the kernel retries: all four correct. If not: L+4 keeps its old bytes | one failed 2 KiB request (the thing under test) |
| L+4 (2nd piece) | SHIFT +4 (default) | piece 2's payload goes to L+8, then piece 3 overwrites it. **L+4 keeps its old bytes**: a lost piece | GOOD everywhere |
| L+8 (3rd piece) | SHIFT −4 | **L+4 holds piece 3's bytes, L+8 keeps its old bytes**: DI-1's §2.2 picture, the stale `WRITE_ADDR1` row of §4.1 | GOOD everywhere |
| L+12 (4th piece) | SHIFT +4 | writes into the **next** block's first piece (L+16). Scratch targets only | GOOD everywhere |

## Host harness

`make -C test/host check` runs `test_fault_knob()`, 18 gates. The knob starts disarmed with
shift +4. REFUSE gives 0xFF / okay FALSE with no doorbell and the medium untouched, counts 1
and disarms. The retry of the same WRITE lands with one doorbell and is not counted. With the
knob armed, a READ of that LBA, a WRITE at another LBA and a WRITE that covers it but starts
earlier are all untouched. SHIFT +4 lands the payload at LBA + 4 once, leaves the original LBA
alone, and the next WRITE lands normally. SHIFT at the unit's last 4 blocks and SHIFT below
LBA 0 refuse. SHIFT −4 on the third of four 2 KiB pieces reproduces DI-1's picture byte for
byte.
