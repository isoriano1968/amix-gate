#!/usr/bin/env python3
# patch_zz9000_cdevsw.py -- register the Z3660 RTG driver (zz9000, major 49)
# after zz9000_040.o has been ld -r'd into the kernel (relink-040-zz9000kv.sh).
#
# cdevsw[49] d_open..d_mmap carry R_68K_32 relocations to `nodev` in the stock
# kernel; this retargets them to zz9000open/close/read/write/ioctl/mmap (the
# same relocation-retarget mechanism as patch_xsvga.py).  The remaining
# fields (d_segmap..d_flag) stay as they are.
#
# The slot offset is derived from the cdevsw symbol (cdevsw + 49*52) and
# asserted against the expected value (cdevsw at .data 0x9dd4,
# amix-z3660rtg DESIGN.md 11.2), so a layout drift aborts instead of
# patching the wrong slot.  Idempotent; fails closed on any mismatch.

import struct, sys

KERNEL = sys.argv[1] if len(sys.argv) > 1 else "build/unix-040-z3660-rtg"
R_68K_32 = 1
CDEVSW_MAJOR = 49
EXPECT_SLOT_OFF = 0xa7c8        # cdevsw 0x9dd4 + 49*52 (amix-z3660rtg DESIGN.md 11.2)
ENTSIZE = 52                     # sizeof(struct cdevsw) on this kernel

# d_open..d_mmap field offsets within one 52-byte cdevsw entry, in order,
# mapped to the driver's entry points.
FIELDS = ["zz9000open", "zz9000close", "zz9000read",
          "zz9000write", "zz9000ioctl", "zz9000mmap"]
EXPECT_CUR = "nodev"


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

    def sym_index(name):
        for i in range(sn):
            if symname(i) == name:
                return i
        raise SystemExit("ABORT: symbol %s not found (was zz9000_040.o ld -r'd in?)" % name)

    def sym_value(name):
        return u32(b, so + sym_index(name) * se + 4)

    cdevsw = sym_value("cdevsw")
    slot_off = cdevsw + CDEVSW_MAJOR * ENTSIZE
    if slot_off != EXPECT_SLOT_OFF:
        raise SystemExit("ABORT: computed cdevsw[%d] offset 0x%x != expected 0x%x "
                          "-- cdevsw layout drifted, re-verify before patching"
                          % (CDEVSW_MAJOR, slot_off, EXPECT_SLOT_OFF))

    rela = byname[".rela.data"]
    ro, re_ = rela["offset"], rela["entsize"]; rn = rela["size"] // re_
    reloc_at = {}
    for i in range(rn):
        o = ro + i * re_; roff = u32(b, o)
        if slot_off <= roff < slot_off + 4 * len(FIELDS):
            reloc_at[roff] = o

    done = skip = 0
    for idx, name in enumerate(FIELDS):
        off = slot_off + idx * 4
        if off not in reloc_at:
            raise SystemExit("ABORT: no .rela.data reloc at cdevsw[%d] 0x%x (%s)"
                              % (CDEVSW_MAJOR, off, name))
        o = reloc_at[off]
        rinfo = u32(b, o + 4); cur = rinfo >> 8; typ = rinfo & 0xff
        if typ != R_68K_32:
            raise SystemExit("ABORT @0x%x: reloc type %d != R_68K_32" % (off, typ))
        tgt = sym_index(name)
        if cur == tgt:
            print("  [skip] cdevsw[%d] @0x%x already -> %s" % (CDEVSW_MAJOR, off, name))
            skip += 1
            continue
        if symname(cur) != EXPECT_CUR:
            raise SystemExit("ABORT @0x%x: current target %s, expected %s"
                              % (off, symname(cur), EXPECT_CUR))
        struct.pack_into(">I", b, o + 4, (tgt << 8) | R_68K_32)
        print("  [ok]   cdevsw[%d] @0x%x  %s -> %s (0x%x)"
              % (CDEVSW_MAJOR, off, EXPECT_CUR, name, sym_value(name)))
        done += 1

    if done and skip:
        raise SystemExit("ABORT: partial cdevsw[%d] retarget (%d/%d) -- atomic group"
                          % (CDEVSW_MAJOR, done, skip))

    open(KERNEL, "wb").write(b)
    print("ZZ9000 (Z3660 RTG) registered (cdevsw[%d], major %d) -> %s" % (CDEVSW_MAJOR, CDEVSW_MAJOR, KERNEL))


main()
