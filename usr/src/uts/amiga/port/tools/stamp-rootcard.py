#!/usr/bin/env python3
"""stamp-rootcard.py -- move a 040-line kernel's root and swap device from the A3000 SCSI
(card 0, c6d0s1/c6d0s2) to another card/unit, e.g. the Z3660 piscsi controller (card 1).

Local stand-in (isoriano, 2026-10-16) for tools/stamp-card1.py, whose kerntools import
(tools/kernel-rootdev-stamp.py) is not in the public amix-kerntools.  Same two fields, same
arithmetic, same write discipline:

  rootdev         .data, 4 bytes: makedevice(18, (slice << 4) | (card << 3) | unit)
  swapfile.bo_name  .data, 128 bytes: "/dev/dsk/c%xd0s%d" % ((card << 3) | unit, slice)

The input must hold the stock card-0 values (rootdev 0x00480016 = c6d0s1, bo_name
/dev/dsk/c6d0s2), no .data relocation may touch either field, and the output may differ from
the input only inside them.

usage: stamp-rootcard.py IN OUT [--card 1] [--unit 6] [--root-slice 1] [--swap-slice 2]
"""
import argparse, os, struct, sys

BLOCK_MAJOR, L_BITSMINOR, L_MAXMIN = 18, 18, 0x3FFFF
BO_FSTYPE, BO_NAME, BO_NAME_LEN = 0, 16, 128      # struct bootobj, measured on the stock image


def die(msg):
    sys.exit("stamp-rootcard: " + msg)


def dev(card, unit, sl):
    return (BLOCK_MAJOR << L_BITSMINOR) | (((sl << 4) | (card << 3) | unit) & L_MAXMIN)


def node(card, unit, sl):
    return "/dev/dsk/c%xd0s%d" % ((card << 3) | unit, sl)


def elf(d):
    if d[:4] != b"\x7fELF" or struct.unpack_from(">H", d, 16)[0] != 1:
        die("not an ELF relocatable kernel")
    shoff, = struct.unpack_from(">I", d, 32)
    shentsize, shnum, shstrndx = struct.unpack_from(">HHH", d, 46)
    sh = [struct.unpack_from(">10I", d, shoff + i * shentsize) for i in range(shnum)]
    strtab = sh[shstrndx]
    def name(i):
        o = strtab[4] + sh[i][0]
        return d[o:d.index(b"\0", o)].decode()
    return sh, name


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inp"); ap.add_argument("out")
    ap.add_argument("--card", type=int, default=1)
    ap.add_argument("--unit", type=int, default=6)
    ap.add_argument("--root-slice", type=int, default=1)
    ap.add_argument("--swap-slice", type=int, default=2)
    a = ap.parse_args()
    if not (0 <= a.card <= 1 and 0 <= a.unit <= 7 and 0 <= a.root_slice <= 7 and 0 <= a.swap_slice <= 7):
        die("card 0-1, unit and slices 0-7")

    d = open(a.inp, "rb").read()
    sh, name = elf(d)
    idx = {name(i): i for i in range(len(sh))}
    data = sh[idx[".data"]]
    symtab = sh[idx[".symtab"]]
    strt = sh[symtab[6]]
    syms = {}
    for o in range(symtab[4], symtab[4] + symtab[5], 16):
        st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from(">IIIBBH", d, o)
        n = d[strt[4] + st_name:d.index(b"\0", strt[4] + st_name)].decode()
        if n in ("rootdev", "swapfile") and st_shndx == idx[".data"]:
            syms[n] = st_value
    if set(syms) != {"rootdev", "swapfile"}:
        die("rootdev/swapfile not found in .data: %r" % syms)
    off_rd = data[4] + syms["rootdev"]
    off_nm = data[4] + syms["swapfile"] + BO_NAME

    # input gate: the stock card-0 values
    rd = struct.unpack_from(">I", d, off_rd)[0]
    nm = d[off_nm:off_nm + BO_NAME_LEN]
    want_rd, want_nm = dev(0, 6, 1), node(0, 6, 2).encode()
    if rd != want_rd:
        die("input rootdev 0x%08x, expected the stock 0x%08x (c6d0s1)" % (rd, want_rd))
    if nm.split(b"\0", 1)[0] != want_nm or nm[len(want_nm):].strip(b"\0"):
        die("input swap name %r, expected %r" % (nm.split(b'\0', 1)[0], want_nm))
    fst = d[data[4] + syms["swapfile"] + BO_FSTYPE:off_nm]
    if fst.strip(b"\0"):
        die("swapfile.bo_fstype is not empty: %r" % fst)

    # no relocation may land in either field
    for i in range(len(sh)):
        if sh[i][1] == 4 and sh[i][7] == idx[".data"]:          # SHT_RELA against .data
            for o in range(sh[i][4], sh[i][4] + sh[i][5], 12):
                r_off = struct.unpack_from(">I", d, o)[0]
                for lo, ln in ((syms["rootdev"], 4), (syms["swapfile"] + BO_NAME, BO_NAME_LEN)):
                    if lo - 4 < r_off < lo + ln:
                        die("relocation at .data+0x%x overlaps a stamped field" % r_off)

    new_rd = dev(a.card, a.unit, a.root_slice)
    new_nm = node(a.card, a.unit, a.swap_slice).encode()
    out = bytearray(d)
    out[off_rd:off_rd + 4] = struct.pack(">I", new_rd)
    out[off_nm:off_nm + BO_NAME_LEN] = new_nm + b"\0" * (BO_NAME_LEN - len(new_nm))

    tmp = a.out + ".tmp"
    with open(tmp, "wb") as f:
        f.write(out); f.flush(); os.fsync(f.fileno())
    os.replace(tmp, a.out)

    back = open(a.out, "rb").read()
    diff = [i for i in range(len(d)) if d[i] != back[i]]
    stray = [i for i in diff if not (off_rd <= i < off_rd + 4 or off_nm <= i < off_nm + BO_NAME_LEN)]
    if len(back) != len(d) or stray:
        os.remove(a.out)
        die("output differs outside the two fields -- removed")
    print("rootdev 0x%08x -> 0x%08x  (%s -> %s)" % (rd, new_rd, node(0, 6, 1),
                                                    node(a.card, a.unit, a.root_slice)))
    print("swap    %s -> %s" % (want_nm.decode(), new_nm.decode()))
    print("%d byte(s) changed, all inside the two fields: %s" % (len(diff), a.out))


main()
