"""elf-global.py OBJ SYMBOL... -- give the named weak definitions of a big-endian ELF32
relocatable object their global binding (STB_WEAK -> STB_GLOBAL in .symtab), in place.

tools/m68k-gas.sh assembles with the symbols a source defines globally declared .weak, so
that m68k-elf and m68k-linux-gnu gas both keep their relocations (gas applies the same
extern policy to weak symbols on every ELF target); this puts the binding back.  Only the
st_info byte of each named symbol changes, and only from weak-defined to global; every
named symbol has to be found exactly once as a weak definition, or nothing is written.
objcopy has no weak -> global (--globalize-symbol takes local symbols only).
"""
import struct
import sys

STB_GLOBAL, STB_WEAK = 1, 2
SHT_SYMTAB = 2


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: elf-global.py OBJ SYMBOL...")
    path, want = sys.argv[1], set(sys.argv[2:])
    d = bytearray(open(path, "rb").read())
    if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 2 or struct.unpack(">H", d[16:18])[0] != 1:
        sys.exit("elf-global: %s: not a big-endian ELF32 relocatable object" % path)
    shoff = struct.unpack(">I", d[32:36])[0]
    shsz, shn = struct.unpack(">HH", d[46:50])
    sh = [struct.unpack(">IIIIIIIIII", d[shoff + i * shsz:shoff + i * shsz + 40]) for i in range(shn)]
    tabs = [s for s in sh if s[1] == SHT_SYMTAB]
    if len(tabs) != 1:
        sys.exit("elf-global: %s: %d symbol tables" % (path, len(tabs)))
    _, _, _, _, off, size, link, _, _, ent = tabs[0]
    stroff = sh[link][4]
    seen = {}
    for i in range(size // ent):
        o = off + i * ent
        name, _, _, info, _, shndx = struct.unpack(">IIIBBH", d[o:o + 16])
        nm = d[stroff + name:d.index(b"\0", stroff + name)].decode()
        if nm not in want:
            continue
        if info >> 4 != STB_WEAK or shndx == 0:
            sys.exit("elf-global: %s: %s is not a weak definition" % (path, nm))
        if nm in seen:
            sys.exit("elf-global: %s: %s twice in the symbol table" % (path, nm))
        seen[nm] = o
    missing = sorted(want - set(seen))
    if missing:
        sys.exit("elf-global: %s: not found: %s" % (path, " ".join(missing)))
    for o in seen.values():
        d[o + 12] = (STB_GLOBAL << 4) | (d[o + 12] & 0xf)
    with open(path, "wb") as f:
        f.write(d)


main()
