#!/usr/bin/env python3
# patch_z3660_intr.py KERNEL -- wire the Z3660 ethernet driver's INT6 receive
# (z3660eth.c built with -DZ3660ETH_STATIC_INTR) into a 68040/68060 kernel
# that already carries it (relink-040-z3660.sh with Z3660ETH_CFLAGS).
#
# The stock kernel has no level-6 chain: p6int clears EXTER and calls one
# handler, the CIA-B one (aciabintr).  Level 2 runs the null-terminated
# int2_tbl (aciaaintr, jbintr, a2091intr, a3091intr_demux, and in slot 4
# aenintr, or the Hydra driver's handler once relink-040-hydra.sh ran).  Two
# R_68K_32 relocations are retargeted, the mechanism of patch_xsvga.py and
# patch_zz9000_cdevsw.py:
#   p6int's call (jsr)      -> z3660eth_p6
#   int2_tbl slot 4         -> z3660eth_p2
# and what each pointed at before goes into the driver's z3660eth_p6_next and
# z3660eth_p2_next (their initialiser relocations), which the wrappers call
# first.  Exactly one site each; both or nothing.  Idempotent.  The driver
# stays polled until z3660eth_intr is 6 in /dev/kmem.

import struct, sys

KERNEL = sys.argv[1]
R_68K_32 = 1
SITES = [  # (relocation section, region, length, the wrapper, its pointer)
    (".rela.text", "p6int", 0x34, "z3660eth_p6", "z3660eth_p6_next"),
    (".rela.data", "int2_tbl", 0x14, "z3660eth_p2", "z3660eth_p2_next"),
]
P2_SLOT = 0x10			# int2_tbl slot 4


def u16(b, o): return struct.unpack(">H", b[o:o + 2])[0]
def u32(b, o): return struct.unpack(">I", b[o:o + 4])[0]


def main():
    b = bytearray(open(KERNEL, "rb").read())
    e_shoff = u32(b, 32); e_shentsize = u16(b, 46); e_shnum = u16(b, 48); e_shstrndx = u16(b, 50)
    sh = []
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        sh.append(dict(name=u32(b, o), type=u32(b, o + 4), offset=u32(b, o + 16),
                       size=u32(b, o + 20), link=u32(b, o + 24), info=u32(b, o + 28),
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

    names = [symname(i) for i in range(sn)]

    def sym_index(name):
        hits = [i for i, n in enumerate(names) if n == name]
        if not hits:
            raise SystemExit("ABORT: symbol %s not found (driver built with -DZ3660ETH_STATIC_INTR?)" % name)
        return hits[-1]

    def sym_value(name):
        return u32(b, so + sym_index(name) * se + 4)

    def relocs(sec, lo, hi):
        rela = byname[sec]
        ro, re_ = rela["offset"], rela["entsize"]
        out = []
        for i in range(rela["size"] // re_):
            o = ro + i * re_; roff = u32(b, o); info = u32(b, o + 4)
            if lo <= roff < hi and (info & 0xff) == R_68K_32:
                out.append((o, roff, names[info >> 8]))
        return out

    plan, skip = [], 0
    for sec, region, length, wrapper, ptr in SITES:
        lo = sym_value(region)
        hits = relocs(sec, lo, lo + length)
        if region == "int2_tbl":
            hits = [h for h in hits if h[1] == lo + P2_SLOT]
        else:			# the call: jsr abs.l (0x4eb9) before it; p6int also loads CACR
            text = sh[byname[sec]["info"]]["offset"]
            hits = [h for h in hits if b[text + h[1] - 2:text + h[1]] == b"\x4e\xb9"]
        if len(hits) != 1:
            raise SystemExit("ABORT: %d relocations in %s+0x%x (%s) -- expected one"
                             % (len(hits), region, 0 if region == "p6int" else P2_SLOT, sec))
        site, roff, cur = hits[0]
        p = relocs(".rela.data", sym_value(ptr), sym_value(ptr) + 4)
        if len(p) != 1:
            raise SystemExit("ABORT: %s has %d initialiser relocations, expected one" % (ptr, len(p)))
        if cur == wrapper:
            print("  [skip] %s+0x%x already -> %s (%s = %s)" % (region, roff - lo, wrapper, ptr, p[0][2]))
            skip += 1
            continue
        if cur.startswith("z3660eth_"):
            raise SystemExit("ABORT: %s+0x%x points at %s" % (region, roff - lo, cur))
        plan.append((site, p[0][0], roff - lo, region, cur, wrapper, ptr))

    if plan and skip:
        raise SystemExit("ABORT: half wired (%d done, %d to do) -- the two go together" % (skip, len(plan)))
    for site, pr, off, region, cur, wrapper, ptr in plan:
        struct.pack_into(">I", b, pr + 4, (sym_index(cur) << 8) | R_68K_32)
        struct.pack_into(">I", b, site + 4, (sym_index(wrapper) << 8) | R_68K_32)
        print("  [ok]   %s+0x%x  %s -> %s; %s = %s" % (region, off, cur, wrapper, ptr, cur))
    open(KERNEL, "wb").write(b)
    print("Z3660 ethernet INT6 receive wired (p6int, int2_tbl) -> %s" % KERNEL)


main()
