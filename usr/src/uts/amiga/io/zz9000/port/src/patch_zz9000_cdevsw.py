#!/usr/bin/env python3
# patch_zz9000_cdevsw.py KERNEL -- register /dev/zz9000 (zz9000.c, the MNT ZZ9000
# on Zorro II) as cdevsw[49], after zz9000_030.o has been ld -r'd into a stock
# 68030 kernel (the AMIX Installer's zz9000 kit, make-kit-030.sh).
#
# The six entry points d_open..d_mmap of slot 49 carry R_68K_32 relocations to
# `nodev` in stock AMIX 2.1 and 2.1p2a; they are retargeted to zz9000open ..
# zz9000mmap, as the integration/kernel.c.txt line
#   zz9000open,zz9000close,zz9000read,zz9000write,zz9000ioctl,zz9000mmap,ND,...
# would compile them.  The slot is derived from the cdevsw symbol (cdevsw +
# 49*52); asserted: cdevsw is whole 52-byte entries up to 49, every field is
# nodev (or already ours: idempotent, all or none).  Fails closed otherwise.
import struct, sys

DEVICE = "/dev/zz9000"
MAJOR = 49
FIELDS = [(0, "zz9000open"), (1, "zz9000close"), (2, "zz9000read"), (3, "zz9000write"),
          (4, "zz9000ioctl"), (5, "zz9000mmap")]
STREAMTAB = None
EXPECT_CUR = "nodev"
R_68K_32 = 1
ENTSIZE = 52                     # sizeof(struct cdevsw) on this kernel
D_XHALT, D_TTYS, D_STR = 9, 10, 11


def u16(b, o): return struct.unpack(">H", b[o:o + 2])[0]
def u32(b, o): return struct.unpack(">I", b[o:o + 4])[0]


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: %s KERNEL" % sys.argv[0])
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
        raise SystemExit("ABORT: symbol %s not defined (was the %s object ld -r'd in?)"
                         % (name, DEVICE))

    ci = sym_index("cdevsw")
    cdevsw, csize = u32(b, so + ci * se + 4), u32(b, so + ci * se + 8)
    if csize % ENTSIZE or csize < (MAJOR + 1) * ENTSIZE:
        raise SystemExit("ABORT: cdevsw is %d bytes: not whole %d-byte entries up to major %d"
                         % (csize, ENTSIZE, MAJOR))
    slot = cdevsw + MAJOR * ENTSIZE
    data = byname[".data"]
    rela = byname[".rela.data"]
    ro, re_ = rela["offset"], rela["entsize"]; rn = rela["size"] // re_
    at = {}
    for i in range(rn):
        o = ro + i * re_; roff = u32(b, o)
        if slot <= roff < slot + ENTSIZE:
            if roff in at:
                raise SystemExit("ABORT: two relocations at cdevsw[%d] 0x%x" % (MAJOR, roff))
            at[roff] = o

    def field(idx):
        """(relocation entry offset or None, its symbol name or None, the stored word)"""
        off = slot + idx * 4
        word = u32(b, data["offset"] + off - data["addr"])
        if off not in at:
            return None, None, word
        o = at[off]
        rinfo = u32(b, o + 4)
        if rinfo & 0xff != R_68K_32 or u32(b, o + 8) != 0:
            raise SystemExit("ABORT: cdevsw[%d] field %d: relocation type %d addend %d, "
                             "expected R_68K_32 + 0" % (MAJOR, idx, rinfo & 0xff, u32(b, o + 8)))
        return o, symname(rinfo >> 8), word

    plan = []                       # (relocation entry, new r_offset or None, new symbol)
    done = 0
    for idx, name in FIELDS:
        o, cur, _ = field(idx)
        if o is None:
            raise SystemExit("ABORT: no relocation at cdevsw[%d] field %d (%s)" % (MAJOR, idx, name))
        if cur == name:
            done += 1
        elif cur == EXPECT_CUR:
            plan.append((o, None, name, idx))
        else:
            raise SystemExit("ABORT: cdevsw[%d] field %d -> %s, expected %s"
                             % (MAJOR, idx, cur, EXPECT_CUR))
    if STREAMTAB:
        # A STREAMS driver is reached through d_str, which nostr leaves 0 with no
        # relocation; the slot's d_xhalt relocation (nodev; no kernel code reads
        # d_xhalt -- spec_*, stropen, qattach, clnopen and the sy* routines use
        # d_open..d_poll, d_str and d_flag only) is moved there and retargeted.
        # d_xhalt is then 0, as for any driver without one.
        xo, xcur, xword = field(D_XHALT)
        so_, scur, sword = field(D_STR)
        to, tcur, tword = field(D_TTYS)
        if to is not None or tword != 0:
            raise SystemExit("ABORT: cdevsw[%d] d_ttys is not 0" % MAJOR)
        if so_ is not None and scur == STREAMTAB and xo is None and xword == 0:
            done += 1
        elif so_ is None and sword == 0 and xo is not None and xcur == EXPECT_CUR:
            plan.append((xo, slot + D_STR * 4, STREAMTAB, D_STR))
        else:
            raise SystemExit("ABORT: cdevsw[%d] d_xhalt/d_str are %s/%s, expected nodev/0 (nostr)"
                             % (MAJOR, xcur or "0x%x" % xword, scur or "0x%x" % sword))
    total = len(FIELDS) + (1 if STREAMTAB else 0)
    if done == total:
        print("  [skip] cdevsw[%d] already -> %s" % (MAJOR, DEVICE))
        return
    if done:
        raise SystemExit("ABORT: cdevsw[%d] partly registered (%d of %d fields): not this kit's state"
                         % (MAJOR, done, total))
    for o, new_off, name, idx in plan:
        tgt = sym_index(name)
        if new_off is not None:
            struct.pack_into(">I", b, o, new_off)
        struct.pack_into(">I", b, o + 4, (tgt << 8) | R_68K_32)
        print("  [ok]   cdevsw[%d] field %d @0x%x -> %s (0x%x)"
              % (MAJOR, idx, slot + idx * 4, name, u32(b, so + tgt * se + 4)))
    open(kernel, "wb").write(b)
    print("%s registered (cdevsw[%d]) -> %s" % (DEVICE, MAJOR, kernel))


main()
