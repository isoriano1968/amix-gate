#!/usr/bin/env python3
# patch_rtgfb_init.py KERNEL -- call rtgfbinit at boot, after rtgfb.o has been
# ld -r'd into the kernel (relink-030-rtgfb.sh, relink-040-rtgfb.sh, the
# installer's kit).
#
# AMIX's main() runs the driver init routines of io_init[] (master.d kernel.c:
# io_init[] = { parinit, 0 }).  The table has no spare slot, so its one entry
# is retargeted: the R_68K_32 relocation of io_init[0] goes from `parinit` to
# `rtgfbinit`, and rtgfbinit calls parinit itself before it searches the
# AutoConfig table.  parinit is called by symbol, so whatever `parinit` is in
# this kernel -- the stock one, or a port kernel's wrapper chain (z3660,
# zz9000, hydra) -- still runs, first, exactly once.
#
# Asserted before anything is written: io_init is 8 bytes (one entry and the
# terminating 0), entry 0 carries an R_68K_32 relocation to `parinit` (or
# already to `rtgfbinit`: then nothing to do), entry 1 is 0 with no
# relocation, and rtgfbinit and parinit are defined.  Fails closed otherwise.

import struct, sys

KERNEL = sys.argv[1]
R_68K_32 = 1
EXPECT_CUR = "parinit"
NEW = "rtgfbinit"


def u16(b, o): return struct.unpack(">H", b[o:o + 2])[0]
def u32(b, o): return struct.unpack(">I", b[o:o + 4])[0]


def main():
    b = bytearray(open(KERNEL, "rb").read())
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

    def sym_index(name, defined=True):
        for i in range(sn):
            if symname(i) == name and (not defined or u16(b, so + i * se + 14) != 0):
                return i
        raise SystemExit("ABORT: symbol %s not defined (was rtgfb.o ld -r'd in?)" % name)

    def sym(name):
        i = sym_index(name)
        return u32(b, so + i * se + 4), u32(b, so + i * se + 8)

    io_init, size = sym("io_init")
    sym_index("parinit")
    tgt = sym_index(NEW)
    if size != 8:
        raise SystemExit("ABORT: io_init is %d bytes, expected 8 ({ parinit, 0 })" % size)

    data = byname[".data"]
    rela = byname[".rela.data"]
    ro, re_ = rela["offset"], rela["entsize"]; rn = rela["size"] // re_
    at = {}
    for i in range(rn):
        o = ro + i * re_; roff = u32(b, o)
        if io_init <= roff < io_init + 8:
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
    if symname(cur) == NEW:
        print("  [skip] io_init[0] @0x%x already -> %s" % (io_init, NEW))
        return
    if symname(cur) != EXPECT_CUR:
        raise SystemExit("ABORT: io_init[0] -> %s, expected %s" % (symname(cur), EXPECT_CUR))
    struct.pack_into(">I", b, o + 4, (tgt << 8) | R_68K_32)
    open(KERNEL, "wb").write(b)
    print("  [ok]   io_init[0] @0x%x  %s -> %s (0x%x); %s calls %s first"
          % (io_init, EXPECT_CUR, NEW, sym(NEW)[0], NEW, EXPECT_CUR))


main()
