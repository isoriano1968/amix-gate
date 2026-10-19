#!/usr/bin/env python3
"""patch_kvseg.py -- grow kvseg from 4 MB to 12 MB in a linked 040/060 kernel.

Usage: python3 src/patch_kvseg.py KERNEL   (in place; KERNEL already contains
src/kvseg040.s, i.e. the symbol kvseg_smsegs_clamp).  See src/kvseg040.s for
why.  Every site is asserted before it is changed; any mismatch stops with no
change written.
"""
import struct, sys

KVSEG_PAGES = 0xc00                    # 12 MB of 4 KB pages (stock 0x400)
KVSEGMAP_OLD, KVSEGMAP_NEW = 0x40440000, 0x40c40000

def die(msg):
    sys.exit("[FAIL] patch_kvseg: " + msg)

path = sys.argv[1]
d = bytearray(open(path, "rb").read())
if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 2:
    die("not a big-endian ELF32 file")
shoff, = struct.unpack_from(">I", d, 32)
shentsize, shnum, shstrndx = struct.unpack_from(">HHH", d, 46)
secs = [list(struct.unpack_from(">IIIIIIIIII", d, shoff + i * shentsize))
        for i in range(shnum)]
def sname(s):
    o = secs[shstrndx][4] + s[0]
    return d[o:d.index(b"\0", o)].decode()
byname = {sname(s): i for i, s in enumerate(secs)}
text = byname[".text"]
toff = secs[text][4]
symi = [i for i, s in enumerate(secs) if s[1] == 2][0]
sym, strt = secs[symi], secs[secs[symi][6]]

def syms():
    for k in range(sym[5] // 16):
        o = sym[4] + k * 16
        nm, val, size, info, other, shndx = struct.unpack_from(">IIIBBH", d, o)
        name = d[strt[4] + nm:d.index(b"\0", strt[4] + nm)].decode()
        yield k, o, name, val, shndx

def find(name):
    hits = [(k, o, v, x) for k, o, n, v, x in syms() if n == name and x != 0]
    if len(hits) != 1:
        die(f"symbol {name}: {len(hits)} definitions")
    return hits[0]

def patch(at, old, new, what):
    o = toff + at
    if bytes(d[o:o + len(old)]) == new:
        die(f"{what} @0x{at:x}: already patched")
    if bytes(d[o:o + len(old)]) != old:
        die(f"{what} @0x{at:x}: expected {old.hex()}, found {d[o:o+len(old)].hex()}")
    d[o:o + len(new)] = new
    print(f"  [ok]   {what:44s} @0x{at:x}  {old.hex()} -> {new.hex()}")

# 1. sysseginit (src/kvm040.s): page tables for KVSEG_PAGES pages
_, _, ssi, x = find("sysseginit")
if x != text:
    die("sysseginit not in .text")
patch(ssi + 0x52, bytes.fromhex("0c81000003ff"),
      bytes.fromhex("0c81") + struct.pack(">I", KVSEG_PAGES - 1),
      "sysseginit loop bound (pages - 1)")
# 2. stock startup: mfree(&sptmap, 0x400, syssegs >> 12)
patch(0x48b64, bytes.fromhex("48780400"),
      bytes.fromhex("4878") + struct.pack(">H", KVSEG_PAGES),
      "startup: sptmap mfree count")
# 3. stock kvm_init: seg_attach(&kas, syssegs, 4 MB, &kvseg)
patch(0x48c7e, bytes.fromhex("2f3c00400000"),
      bytes.fromhex("2f3c") + struct.pack(">I", KVSEG_PAGES << 12),
      "kvm_init: kvseg seg_attach size")
# 4. stock kvm_init: movel %d0,smsegs -> jsr kvseg_smsegs_clamp
clamp, _, _, x = find("kvseg_smsegs_clamp")
smsegs = find("smsegs")[0]
rela = [i for i, s in enumerate(secs) if s[1] == 4 and s[7] == text]
if len(rela) != 1:
    die("no single .rela.text")
r = secs[rela[0]]
hit = None
for k in range(r[5] // 12):
    o = r[4] + k * 12
    off, info, add = struct.unpack_from(">IIi", d, o)
    if off == 0x48cb6:
        hit = (o, info, add)
if not hit or hit[1] >> 8 != smsegs or hit[2] != 0:
    die("relocation at 0x48cb6 is not smsegs+0")
patch(0x48cb4, bytes.fromhex("23c0"), bytes.fromhex("4eb9"),
      "kvm_init: movel %d0,smsegs -> jsr")
struct.pack_into(">I", d, hit[0] + 4, (clamp << 8) | (hit[1] & 0xff))
print(f"  [ok]   {'reloc @0x48cb6: smsegs -> kvseg_smsegs_clamp':44s}")
# 5. kvsegmap (absolute): the segmap window starts after the larger kvseg
k, o, v, x = find("kvsegmap")
if x != 0xfff1 or v != KVSEGMAP_OLD:
    die(f"kvsegmap is 0x{v:x} (shndx {x:#x}), expected ABS 0x{KVSEGMAP_OLD:x}")
struct.pack_into(">I", d, o + 4, KVSEGMAP_NEW)
print(f"  [ok]   {'kvsegmap':44s} 0x{KVSEGMAP_OLD:x} -> 0x{KVSEGMAP_NEW:x}")
open(path, "wb").write(d)
print(f"[OK] kvseg {KVSEG_PAGES * 4 // 1024} MB, segmap window "
      f"0x{KVSEGMAP_NEW:x}-0x48440000: {path}")
