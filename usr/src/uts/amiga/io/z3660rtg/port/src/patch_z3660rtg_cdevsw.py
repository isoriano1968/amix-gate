#!/usr/bin/env python3
# patch_z3660rtg_cdevsw.py KERNEL -- register /dev/z3660rtg (io/z3660rtg, the
# Z3660 accelerator's RTG) as cdevsw[55] after z3660rtg_040.o has been ld -r'd
# into a 68040/68060 port kernel (the AMIX Installer's z3660rtg-040 kit).  The
# same relocation-retarget mechanism as patch_rtgfb_cdevsw.py: d_open, d_close,
# d_read, d_write, d_ioctl and d_mmap of slot 55 carry R_68K_32 relocations to
# `nodev` and are retargeted to z3660rtgopen/close/read/write/ioctl/mmap; the
# remaining fields (d_segmap..d_flag) stay as they are.
# Major 55 is nodev in stock AMIX 2.1c and 2.1p2a and in the port kernels,
# generic and z3660 variant (46 audio, 47 Hydra, 48 random, 49 zz9000, 50 sad,
# 51 zen0, 52 zz9000eth, 53 paula, 54 rtgfb, 56 xsurf100, 67 Xsvga,
# 68 VA2000), checked 2026-11-17.
#
# The slot is DERIVED from the cdevsw symbol (cdevsw + 55*52); what is asserted
# instead of an address: cdevsw is a whole number of 52-byte entries and has
# slot 55, and every field this retargets is an R_68K_32 relocation to `nodev`
# (or already ours).  Idempotent; fails closed on any mismatch, all six fields
# or none.

import struct, sys

KERNEL = sys.argv[1]
R_68K_32 = 1
CDEVSW_MAJOR = 55
ENTSIZE = 52                     # sizeof(struct cdevsw) on this kernel

# (field index within the 52-byte entry, entry point): d_open .. d_mmap
FIELDS = [(0, "z3660rtgopen"), (1, "z3660rtgclose"), (2, "z3660rtgread"),
          (3, "z3660rtgwrite"), (4, "z3660rtgioctl"), (5, "z3660rtgmmap")]
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
            if symname(i) == name and u16(b, so + i * se + 14) != 0:
                return i
        raise SystemExit("ABORT: symbol %s not defined (was z3660rtg_040.o ld -r'd in?)" % name)

    def sym_value(name):
        return u32(b, so + sym_index(name) * se + 4)

    ci = sym_index("cdevsw")
    cdevsw, csize = u32(b, so + ci * se + 4), u32(b, so + ci * se + 8)
    if csize % ENTSIZE or csize < (CDEVSW_MAJOR + 1) * ENTSIZE:
        raise SystemExit("ABORT: cdevsw is %d bytes: not whole %d-byte entries up to major %d"
                         % (csize, ENTSIZE, CDEVSW_MAJOR))
    slot_off = cdevsw + CDEVSW_MAJOR * ENTSIZE

    rela = byname[".rela.data"]
    ro, re_ = rela["offset"], rela["entsize"]; rn = rela["size"] // re_
    reloc_at = {}
    for i in range(rn):
        o = ro + i * re_; roff = u32(b, o)
        if slot_off <= roff < slot_off + ENTSIZE:
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
    print("/dev/z3660rtg registered (cdevsw[%d], major %d) -> %s" % (CDEVSW_MAJOR, CDEVSW_MAJOR, KERNEL))


main()
