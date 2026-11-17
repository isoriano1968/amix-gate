#!/usr/bin/env python3
# patch_io_init.py KERNEL SYMBOL -- make io_init[0] call SYMBOL instead of parinit,
# after the object that defines SYMBOL has been ld -r'd into KERNEL.
#
# AMIX's main() runs the routines of io_init[] once at boot (master.d kernel.c:
# io_init[] = { parinit, 0 }).  The table has no spare slot, so a kernel that
# needs more boot routines gets ONE chain stub -- amixdrv_init, generated from
# amixdrv_init.s.in: jsr parinit, jsr each driver's init, rts -- and this
# script retargets the R_68K_32 relocation of io_init[0] from `parinit` to it.
# Nothing else is written: no bytes of code or data, one relocation's symbol.
#
# The same file goes into every driver kit (rtgfb, zz9000, hydra, va2000) and
# the kit-tools package; the AMIX Installer runs it once, after every kit's own
# patch_*.py, with the stub it generated.  relink-030-rtgfb.sh and
# relink-040-rtgfb.sh use it the same way.
#
# Asserted before anything is written, fails closed otherwise:
#   - io_init is 8 bytes: one entry and the terminating 0;
#   - io_init[1] is 0 and carries no relocation;
#   - io_init[0] carries one R_68K_32 relocation with addend 0, to `parinit`
#     (or already to SYMBOL: then nothing to do -- idempotent);
#   - SYMBOL and parinit are defined (not UND) global symbols in .text.

import struct
import sys

R_68K_32 = 1
EXPECT_CUR = "parinit"
STB_GLOBAL = 1


def u16(b, o): return struct.unpack(">H", b[o:o + 2])[0]
def u32(b, o): return struct.unpack(">I", b[o:o + 4])[0]


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: patch_io_init.py KERNEL SYMBOL")
    kernel, new = sys.argv[1], sys.argv[2]
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
        """index of the one strong global definition of `name` (a weakened
        base symbol beside it is fine), which must be in `section`"""
        hits = []
        for i in range(sn):
            o = so + i * se
            shndx = u16(b, o + 14)
            if symname(i) == name and shndx != 0 and b[o + 12] >> 4 == STB_GLOBAL:
                hits.append(i)
        if len(hits) != 1:
            raise SystemExit("ABORT: %s: %d strong global definitions, expected 1%s"
                             % (name, len(hits), " (was its object ld -r'd in?)" if not hits else ""))
        i = hits[0]
        shndx = u16(b, so + i * se + 14)
        if shndx >= len(sh) or sh[shndx]["nm"] != section:
            raise SystemExit("ABORT: %s is not in %s" % (name, section))
        return i

    io = defined("io_init", ".data")
    io_init, size = u32(b, so + io * se + 4), u32(b, so + io * se + 8)
    defined(EXPECT_CUR, ".text")
    tgt = defined(new, ".text")
    if size != 8:
        raise SystemExit("ABORT: io_init is %d bytes, expected 8 ({ parinit, 0 })" % size)

    data = byname[".data"]; rela = byname[".rela.data"]
    ro, re_ = rela["offset"], rela["entsize"]; rn = rela["size"] // re_
    at = {}
    for i in range(rn):
        o = ro + i * re_; roff = u32(b, o)
        if io_init <= roff < io_init + 8:
            if roff in at:
                raise SystemExit("ABORT: two relocations at 0x%x" % roff)
            at[roff] = o
    if io_init + 4 in at or u32(b, data["offset"] + io_init + 4 - data["addr"]) != 0:
        raise SystemExit("ABORT: io_init[1] is not the terminating 0")
    if io_init not in at:
        raise SystemExit("ABORT: no .rela.data relocation at io_init[0] (0x%x)" % io_init)
    o = at[io_init]
    rinfo = u32(b, o + 4); cur = rinfo >> 8; typ = rinfo & 0xff
    if typ != R_68K_32 or u32(b, o + 8) != 0:
        raise SystemExit("ABORT: io_init[0] relocation type %d addend %d, expected R_68K_32 + 0"
                         % (typ, u32(b, o + 8)))
    if symname(cur) == new:
        print("  [skip] io_init[0] @0x%x already -> %s" % (io_init, new))
        return
    if symname(cur) != EXPECT_CUR:
        raise SystemExit("ABORT: io_init[0] -> %s, expected %s" % (symname(cur), EXPECT_CUR))
    struct.pack_into(">I", b, o + 4, (tgt << 8) | R_68K_32)
    open(kernel, "wb").write(b)
    print("  [ok]   io_init[0] @0x%x  %s -> %s (0x%x)"
          % (io_init, EXPECT_CUR, new, u32(b, so + tgt * se + 4)))


main()
