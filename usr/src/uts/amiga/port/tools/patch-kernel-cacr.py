#!/usr/bin/env python3
"""patch-kernel-cacr.py -- set the CACR value an AMIX 040/060 port kernel enables at boot.

pstart040.s loads one immediate into %d0 and stores it to the `cacr` and
`sup_cacr` globals, which every exception handler reloads into CACR:

    movel  &0x80008000,%d0      203c 8000 8000
    movel  %d0,cacr             23c0 xxxx xxxx

This rewrites that immediate, and nothing else, in a copy of the kernel.
0xa0008000 adds the 68060 store buffer (ESB, bit 29) to data + instruction
cache; measured on the A3000/Z3660 2026-10-26 (framebuffer stores ~30 %
faster with the framebuffer in the noncacheable-not-serialised class).
0xa0c08000 adds the branch cache (EBC, bit 23) with CABC (bit 22): every
exception entry reloads sup_cacr, so the branch cache is cleared on every
kernel entry -- before any exec, page-in, COW, ptrace write or process
switch -- which covers the invalidation duty without patching each site
(Dhrystone 71.4k -> 91.1k/s on the A3000/Z3660 at 80 MHz).
Bits 29, 23 and 22 are 68060-only: use such a kernel on a 68060 only.

    patch-kernel-cacr.py IN OUT [VALUE]      VALUE default 0xa0008000
"""
import sys

def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    value = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0xa0008000
    data = bytearray(open(src, "rb").read())
    old = bytes.fromhex("203c80008000") + bytes.fromhex("23c0")
    hits = [i for i in range(len(data) - len(old)) if data[i:i + len(old)] == old]
    if len(hits) != 1:
        sys.exit("expected exactly one `movel #0x80008000,%%d0; movel %%d0,cacr`, found %d" % len(hits))
    at = hits[0]
    data[at + 2:at + 6] = value.to_bytes(4, "big")
    open(dst, "wb").write(data)
    print("%s: CACR immediate at file offset 0x%x: 80008000 -> %08x" % (dst, at + 2, value))

main()
