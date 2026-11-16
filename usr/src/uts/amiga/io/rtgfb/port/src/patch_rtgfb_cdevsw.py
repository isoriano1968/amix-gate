#!/usr/bin/env python3
# patch_rtgfb_cdevsw.py -- register /dev/rtgfb (src/rtgfb.c) as cdevsw[54] after
# rtgfb.o has been ld -r'd into the kernel (relink-030-rtgfb.sh on a stock 68030
# kernel, relink-040-rtgfb.sh on a 68040/68060 port kernel).  The same
# relocation-retarget mechanism as patch_random_cdevsw.py: d_open, d_close,
# d_ioctl and d_mmap of slot 54 carry R_68K_32 relocations to `nodev` and are
# retargeted to rtgfbopen/rtgfbclose/rtgfbioctl/rtgfbmmap; d_read and d_write
# stay nodev (the device has no read/write).
# Major 54 is nodev in stock AMIX 2.1c and 2.1p2a and in the port kernels
# (48 random, 49 zz9000, 51 zen0, 53 paula, 67 Xsvga, 68 VA2000 are taken).
#
# The slot offset is DERIVED from the cdevsw symbol (cdevsw + 54*52) and
# asserted against cdevsw at .data 0x9dd4 (the same in the stock 2.1c/2.1p2a
# kernels and every port kernel), so a layout drift aborts instead of patching
# the wrong slot.  Idempotent; fails closed on any mismatch.

import struct, sys

KERNEL = sys.argv[1]
R_68K_32 = 1
CDEVSW_MAJOR = 54
EXPECT_SLOT_OFF = 0x9dd4 + 54 * 52
ENTSIZE = 52                     # sizeof(struct cdevsw) on this kernel

# (field index within the 52-byte entry, entry point): d_open, d_close, d_ioctl, d_mmap
FIELDS = [(0, "rtgfbopen"), (1, "rtgfbclose"), (4, "rtgfbioctl"), (5, "rtgfbmmap")]
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
        raise SystemExit("ABORT: symbol %s not found (was rtgfb.o ld -r'd in?)" % name)

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
        if slot_off <= roff < slot_off + 4 * 6:
            reloc_at[roff] = o

    done = skip = 0
    for idx, name in FIELDS:
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
    print("/dev/rtgfb registered (cdevsw[%d], major %d) -> %s" % (CDEVSW_MAJOR, CDEVSW_MAJOR, KERNEL))


main()
