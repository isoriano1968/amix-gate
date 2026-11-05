#!/usr/bin/env python3
# patch_shminfo.py -- raise the System V shared memory limits in shminfo
# (relink-040-shm.sh, 2026-10-29).
#
# shminfo is four longs in .data: shmmax, shmmin, shmmni, shmseg.  The stock
# values are 327680, 1, 100, 6.  A 1920x1080 picture at 32 bits is 8294400
# bytes, and a program that draws through MIT-SHM in strips needs more than
# six segments (the X server attaches every client's segment as well), so:
#
#   shmmax  327680 -> 16777216   shmget compares the size and nothing else;
#                                the pages come from anon_resv as before
#   shmseg       6 -> 32         shmat compares a signed byte in the u area
#                                (u+0x378, extb.l) against it: <= 127
#   shmmin, shmmni unchanged     shmmni is the size of the static shmem table
#
# The symbol's .data offset is asserted against the value measured on the
# A3000/Z3660 kernel (0xaed4), and the old values are asserted before
# anything is written.  Idempotent; fails closed on any mismatch.

import struct, sys

KERNEL = sys.argv[1]
EXPECT_OFF = 0xaed4
OLD = (327680, 1, 100, 6)
NEW = (16777216, 1, 100, 32)
def u16(b, o): return struct.unpack(">H", b[o:o + 2])[0]
def u32(b, o): return struct.unpack(">I", b[o:o + 4])[0]


def main():
    b = bytearray(open(KERNEL, "rb").read())
    e_shoff = u32(b, 32); e_shentsize = u16(b, 46); e_shnum = u16(b, 48); e_shstrndx = u16(b, 50)
    sh = []
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        sh.append(dict(name=u32(b, o), offset=u32(b, o + 16), size=u32(b, o + 20),
                       link=u32(b, o + 24), entsize=u32(b, o + 36)))
    shstr = sh[e_shstrndx]["offset"]

    def nm(s):
        e = b.index(b"\0", shstr + s["name"]); return b[shstr + s["name"]:e].decode()

    byname = {nm(s): s for s in sh}
    symtab = byname[".symtab"]; strtab = sh[symtab["link"]]
    so, se = symtab["offset"], symtab["entsize"]; sn = symtab["size"] // se
    stroff = strtab["offset"]
    data = byname[".data"]
    data_idx = [i for i, s in enumerate(sh) if nm(s) == ".data"][0]

    off = None
    for i in range(sn):
        o = so + i * se; n = u32(b, o); e = b.index(b"\0", stroff + n)
        if b[stroff + n:e] == b"shminfo":
            if u16(b, o + 14) != data_idx:
                raise SystemExit("ABORT: shminfo is not a .data symbol")
            off = u32(b, o + 4)
            break
    if off is None:
        raise SystemExit("ABORT: symbol shminfo not found")
    if off != EXPECT_OFF:
        raise SystemExit("ABORT: shminfo at .data 0x%x, expected 0x%x -- layout drifted, "
                         "re-verify before patching" % (off, EXPECT_OFF))
    if off + 16 > data["size"]:
        raise SystemExit("ABORT: shminfo runs past the end of .data")

    fo = data["offset"] + off
    cur = struct.unpack(">4I", b[fo:fo + 16])
    if cur == NEW:
        print("  [skip] shminfo already %s" % (NEW,))
        return
    if cur != OLD:
        raise SystemExit("ABORT: shminfo is %s, expected the stock %s" % (cur, OLD))
    struct.pack_into(">4I", b, fo, *NEW)
    open(KERNEL, "wb").write(b)
    print("  [ok]   shminfo @.data 0x%x  shmmax %d -> %d, shmseg %d -> %d"
          % (off, OLD[0], NEW[0], OLD[3], NEW[3]))
    print("shared memory limits raised -> %s" % KERNEL)


main()
