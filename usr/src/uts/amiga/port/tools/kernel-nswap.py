#!/usr/bin/env python3
"""kernel-nswap.py -- the swap size an AMIX installation writes into /stand/unix.

  kernel-nswap.py KERNEL             prints "<code sha256> <nswap>"
  kernel-nswap.py KERNEL N OUT       writes KERNEL with nswap = N to OUT

The AMIX installation stores the size of the UNIX_Swap partition (512-byte
blocks) in swapfile.nswap, .data swapfile+0x98, 4 bytes.  Nothing else in
the image differs between installations of the same release (the public
2.1c kernels/2.1c/unix has 61440, a 2.1p2a test disk 409600).  The code
sha256 is the sha256 of the image with those 4 bytes set to 0: one value
for every installation of 2.1c, which tools/verify-stock.sh and
tools/amix-kernel-040 accept.
"""
import hashlib
import struct
import sys


def main():
    if len(sys.argv) not in (2, 4):
        sys.exit(__doc__.strip())
    path = sys.argv[1]
    d = bytearray(open(path, "rb").read())
    if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 2:
        sys.exit("kernel-nswap: %s: not a 32-bit big-endian ELF" % path)
    shoff = struct.unpack(">I", d[32:36])[0]
    shsz, shn, shstr = struct.unpack(">HHH", d[46:52])
    sh = [struct.unpack(">IIIIIIIIII", d[shoff + i * shsz:shoff + i * shsz + 40])
          for i in range(shn)]

    def name(off, tab):
        s = sh[tab][4] + off
        return bytes(d[s:d.index(0, s)]).decode()

    secs = {name(s[0], shstr): (i, s) for i, s in enumerate(sh)}
    if ".symtab" not in secs or ".data" not in secs:
        sys.exit("kernel-nswap: %s: no .symtab or .data" % path)
    sym = secs[".symtab"][1]
    data_idx, data = secs[".data"]
    swap = None
    for o in range(sym[4], sym[4] + sym[5], 16):
        st_name, val, size, info, other, shndx = struct.unpack(">IIIBBH", d[o:o + 16])
        if shndx == data_idx and info >> 4 == 1 and name(st_name, sym[6]) == "swapfile":
            swap = val
    if swap is None:
        sys.exit("kernel-nswap: %s: no global swapfile in .data" % path)
    off = data[4] + swap + 0x98
    if len(sys.argv) == 4:
        d[off:off + 4] = struct.pack(">I", int(sys.argv[2], 0))
        open(sys.argv[3], "wb").write(d)
        return
    nswap = struct.unpack(">I", d[off:off + 4])[0]
    d[off:off + 4] = b"\0\0\0\0"
    print(hashlib.sha256(d).hexdigest(), nswap)


main()
