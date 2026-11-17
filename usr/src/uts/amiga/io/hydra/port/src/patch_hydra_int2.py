#!/usr/bin/env python3
# patch_hydra_int2.py KERNEL -- route the level-2 interrupt to hydraintr as
# well, after hydra_030.o and hydra_int2.o have been ld -r'd into a stock
# 68030 kernel (the AMIX Installer's hydra kit, make-kit-030.sh).
#
# The level-2 dispatcher (amiga/ml/ttrap.s p2int) calls every entry of
# int2_tbl[]; stock 2.1/2.1p2a has { aciaaintr, jbintr, a2091intr, a3091intr,
# aenintr, 0 } and no spare slot.  The one R_68K_32 relocation of the aenintr
# entry is retargeted to hydra_int2 (src/hydra_int2.s), which calls aenintr
# and then hydraintr: the same table-retarget mechanism as the port's
# patch_a3091_intr.py.  Nothing else is written.
#
# Asserted, by symbol (no fixed addresses): int2_tbl is a .data table of
# st_size/4 words ending in a 0 without relocation; every other entry is an
# R_68K_32 relocation with addend 0; exactly one of them goes to aenintr (or
# already to hydra_int2: idempotent); no entry goes to hydraintr (a kernel
# built from source with the Hydra already has it); hydra_int2, hydraintr and
# aenintr are defined.  Fails closed otherwise.

import struct
import sys

R_68K_32 = 1
OLD, NEW, BODY = "aenintr", "hydra_int2", "hydraintr"


def u16(b, o): return struct.unpack(">H", b[o:o + 2])[0]
def u32(b, o): return struct.unpack(">I", b[o:o + 4])[0]


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_hydra_int2.py KERNEL")
    kernel = sys.argv[1]
    b = bytearray(open(kernel, "rb").read())
    e_shoff = u32(b, 32); e_shentsize = u16(b, 46); e_shnum = u16(b, 48); e_shstrndx = u16(b, 50)
    sh = []
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        sh.append(dict(name=u32(b, o), type=u32(b, o + 4), addr=u32(b, o + 12),
                       offset=u32(b, o + 16), size=u32(b, o + 20), link=u32(b, o + 24),
                       entsize=u32(b, o + 36)))
    shstr = sh[e_shstrndx]["offset"]

    def nm(s):
        e = b.index(b"\0", shstr + s["name"]); return b[shstr + s["name"]:e].decode()

    byname = {nm(s): s for s in sh}
    symtab = byname[".symtab"]; strtab = sh[symtab["link"]]
    so, se = symtab["offset"], symtab["entsize"]; sn = symtab["size"] // se
    stroff = strtab["offset"]

    def symname(i):
        o = so + i * se; n = u32(b, o); e = b.index(b"\0", stroff + n); return b[stroff + n:e].decode()

    def sym_index(name):
        for i in range(sn):
            if symname(i) == name and u16(b, so + i * se + 14) != 0:
                return i
        raise SystemExit("ABORT: symbol %s not defined (were hydra_030.o and hydra_int2.o "
                         "ld -r'd in?)" % name)

    ti = sym_index("int2_tbl")
    tbl, size = u32(b, so + ti * se + 4), u32(b, so + ti * se + 8)
    tgt = sym_index(NEW)
    sym_index(BODY); sym_index(OLD)
    if size < 8 or size % 4:
        raise SystemExit("ABORT: int2_tbl is %d bytes" % size)
    data = byname[".data"]; rela = byname[".rela.data"]
    ro, re_ = rela["offset"], rela["entsize"]; rn = rela["size"] // re_
    at = {}
    for i in range(rn):
        o = ro + i * re_; roff = u32(b, o)
        if tbl <= roff < tbl + size:
            if roff in at:
                raise SystemExit("ABORT: two relocations at int2_tbl+%d" % (roff - tbl))
            at[roff] = o
    last = tbl + size - 4
    if last in at or u32(b, data["offset"] + last - data["addr"]) != 0:
        raise SystemExit("ABORT: int2_tbl does not end in its terminating 0")
    names = []
    for off in range(tbl, last, 4):
        if off not in at:
            raise SystemExit("ABORT: int2_tbl[%d] has no relocation" % ((off - tbl) // 4))
        o = at[off]
        rinfo = u32(b, o + 4)
        if rinfo & 0xff != R_68K_32 or u32(b, o + 8) != 0:
            raise SystemExit("ABORT: int2_tbl[%d]: relocation type %d addend %d"
                             % ((off - tbl) // 4, rinfo & 0xff, u32(b, o + 8)))
        names.append((off, o, symname(rinfo >> 8)))
    print("  int2_tbl: %s" % ", ".join(n for _, _, n in names))
    if any(n == BODY for _, _, n in names):
        raise SystemExit("ABORT: int2_tbl already calls %s directly" % BODY)
    if [n for _, _, n in names].count(NEW) == 1 and not any(n == OLD for _, _, n in names):
        print("  [skip] int2_tbl already -> %s" % NEW)
        return
    hits = [(off, o) for off, o, n in names if n == OLD]
    if len(hits) != 1 or any(n == NEW for _, _, n in names):
        raise SystemExit("ABORT: int2_tbl has %d %s entries, expected exactly 1" % (len(hits), OLD))
    off, o = hits[0]
    struct.pack_into(">I", b, o + 4, (tgt << 8) | R_68K_32)
    open(kernel, "wb").write(b)
    print("  [ok]   int2_tbl[%d] @0x%x  %s -> %s (0x%x): %s, then %s"
          % ((off - tbl) // 4, off, OLD, NEW, u32(b, so + tgt * se + 4), OLD, BODY))


main()
