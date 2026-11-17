#!/usr/bin/env python3
# patch_int2.py KERNEL SYMBOL -- make int2_tbl's aenintr entry call SYMBOL
# instead, after the object that defines SYMBOL has been ld -r'd into KERNEL.
#
# The level-2 dispatcher (amiga/ml/ttrap.s p2int) calls every entry of
# int2_tbl[]; stock 2.1c/2.1p2a and the port kernels have { aciaaintr, jbintr,
# a2091intr, a3091intr, aenintr, 0 } and no spare slot.  A kernel whose
# drivers need the level-2 interrupt gets ONE chain stub -- amixdrv_int2,
# generated from amixdrv_int2.s.in: aenintr, then each driver's KIT.info
# intr= routine, rts -- and this script retargets the R_68K_32 relocation of
# the aenintr entry to it: the table-retarget mechanism of patch_io_init.py
# and the port's patch_a3091_intr.py.  Nothing else is written: no bytes of
# code or data, one relocation's symbol.
#
# The same file goes into every driver kit with an intr= and into the
# kit-tools package; the AMIX Installer runs it once, after every kit's own
# patch_*.py and before patch_io_init.py, with the stub it generated
# (port/tools/kit-relink.sh does the same).
#
# Asserted, by symbol (no fixed addresses), before anything is written; fails
# closed otherwise:
#   - int2_tbl is a .data table of st_size/4 words ending in a 0 without
#     relocation; every other entry is one R_68K_32 relocation with addend 0;
#   - exactly one entry goes to aenintr (or, idempotent, none does and
#     exactly one already goes to SYMBOL: then nothing to do);
#   - SYMBOL and aenintr are defined (not UND) global symbols in .text.

import struct
import sys

R_68K_32 = 1
OLD = "aenintr"
STB_GLOBAL = 1


def u16(b, o): return struct.unpack(">H", b[o:o + 2])[0]
def u32(b, o): return struct.unpack(">I", b[o:o + 4])[0]


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: patch_int2.py KERNEL SYMBOL")
    kernel, new = sys.argv[1], sys.argv[2]
    if new == OLD:
        raise SystemExit("ABORT: SYMBOL must not be %s itself" % OLD)
    b = bytearray(open(kernel, "rb").read())
    if b[:4] != b"\x7fELF" or b[4] != 1 or b[5] != 2 or u16(b, 18) != 4:
        raise SystemExit("ABORT: %s is not a big-endian 32-bit m68k ELF file" % kernel)
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

    for s in sh:
        s["nm"] = nm(s)
    byname = {s["nm"]: s for s in sh}
    for need in (".symtab", ".data", ".rela.data"):
        if need not in byname:
            raise SystemExit("ABORT: no %s section (a stripped kernel cannot be relinked)" % need)
    symtab = byname[".symtab"]; strtab = sh[symtab["link"]]
    so, se = symtab["offset"], symtab["entsize"]; sn = symtab["size"] // se
    stroff = strtab["offset"]

    def symname(i):
        o = so + i * se; n = u32(b, o); e = b.index(b"\0", stroff + n); return b[stroff + n:e].decode()

    def defined(name, section):
        """index of the one strong global definition of `name`, in `section`"""
        hits = []
        for i in range(sn):
            o = so + i * se
            if symname(i) == name and u16(b, o + 14) != 0 and b[o + 12] >> 4 == STB_GLOBAL:
                hits.append(i)
        if len(hits) != 1:
            raise SystemExit("ABORT: %s: %d strong global definitions, expected 1%s"
                             % (name, len(hits), " (was its object ld -r'd in?)" if not hits else ""))
        i = hits[0]
        shndx = u16(b, so + i * se + 14)
        if shndx >= len(sh) or sh[shndx]["nm"] != section:
            raise SystemExit("ABORT: %s is not in %s" % (name, section))
        return i

    ti = defined("int2_tbl", ".data")
    tbl, size = u32(b, so + ti * se + 4), u32(b, so + ti * se + 8)
    tgt = defined(new, ".text")
    defined(OLD, ".text")
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
    olds = [(off, o) for off, o, n in names if n == OLD]
    news = [n for _, _, n in names if n == new]
    if not olds and len(news) == 1:
        print("  [skip] int2_tbl already -> %s" % new)
        return
    if len(olds) != 1 or news:
        raise SystemExit("ABORT: int2_tbl has %d %s and %d %s entries, expected 1 and 0"
                         % (len(olds), OLD, len(news), new))
    off, o = olds[0]
    struct.pack_into(">I", b, o + 4, (tgt << 8) | R_68K_32)
    open(kernel, "wb").write(b)
    print("  [ok]   int2_tbl[%d] @0x%x  %s -> %s (0x%x)"
          % ((off - tbl) // 4, off, OLD, new, u32(b, so + tgt * se + 4)))


main()
