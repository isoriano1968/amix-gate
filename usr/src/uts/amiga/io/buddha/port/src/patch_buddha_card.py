#!/usr/bin/env python3
# patch_buddha_card.py KERNEL -- register the Buddha and Catweasel Z-II IDE
# driver (budqueue, cwqueue) with sd.c's controller selector, after
# buddha_<line>.o and buddha_card.o (the five-row table buddha_scsicard) have
# been ld -r'd into an AMIX kernel (the AMIX Installer's buddha kit; both
# kernel lines, the stock 68030 one and the 68040/68060 port).
#
# The boards are ordinary SCSI controllers to AMIX: no device switch slot of
# their own.  The disks stay the stock dd driver's (bdevsw[18], cdevsw[40],
# /dev/dsk and /dev/rdsk cNdUsS); sd.c's local init() finds the controllers
# by walking scsicard[] -- three rows {product, queue, name} -- with
# autocon().  Two edits, as patch_z3660scsi_card.py makes them (located by
# symbol and asserted rather than taken from fixed offsets):
#
#   1. the one .rela.text relocation naming the local `scsicard` (the operand
#      of init()'s `lea scsicard,%a3`, opcode 0x47f9) is retargeted to
#      buddha_scsicard;
#   2. init()'s loop bound, `addqw #1,%a2; moveq #2,%d1; cmpl %a2,%d1; bccw`
#      (524a 7202 b28a 6400), found once within 0x80 bytes after that
#      operand: moveq #2 -> moveq #4, one byte, so the walk takes five rows.
#
# Asserted before anything is written, fails closed otherwise: scsicard is
# three rows in .data whose queue words relocate to a3091queue, a2090queue,
# a2091queue and whose names are the strings buddha_scsicard's rows 0-2
# carry; the new table's rows relocate to the same three functions, then
# budqueue (0x12120000) and cwqueue (0x1212002a).  A kernel whose table is
# already another kit's (the z3660scsi kit's z3660_scsicard) is refused with
# that reason: the two kits each bring their own table and cannot be linked
# together yet.  Idempotent: an already registered kernel is left as is.
import struct, sys

R_68K_32 = 1
STOCK = "scsicard"
TABLE = "buddha_scsicard"
QUEUES = ("a3091queue", "a2090queue", "a2091queue", "budqueue", "cwqueue")
PRODUCTS = (0x12120000, 0x1212002a)          # rows 3 and 4
OTHER = ("z3660_scsicard",)                  # another kit's table
LOOP = bytes.fromhex("524a7202b28a6400")     # addqw #1,%a2; moveq #2,%d1; cmpl %a2,%d1; bccw
LOOP_NEW = bytes.fromhex("524a7204b28a6400")
BOUND = 3                                    # offset of moveq's immediate byte in LOOP
LEA_A3 = 0x47f9                              # lea abs.l,%a3


def u16(b, o): return struct.unpack(">H", b[o:o + 2])[0]
def u32(b, o): return struct.unpack(">I", b[o:o + 4])[0]


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: %s KERNEL" % sys.argv[0])
    kernel = sys.argv[1]
    b = bytearray(open(kernel, "rb").read())
    if b[:4] != b"\x7fELF" or b[4] != 1 or b[5] != 2 or u16(b, 18) != 4:
        raise SystemExit("ABORT: %s is not a big-endian 32-bit m68k ELF file" % kernel)
    e_shoff = u32(b, 32); e_shentsize = u16(b, 46); e_shnum = u16(b, 48); e_shstrndx = u16(b, 50)
    sh = []
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        sh.append(dict(name=u32(b, o), type=u32(b, o + 4), addr=u32(b, o + 12),
                       offset=u32(b, o + 16), size=u32(b, o + 20), link=u32(b, o + 24),
                       info=u32(b, o + 28), entsize=u32(b, o + 36)))
    shstr = sh[e_shstrndx]["offset"]

    def nm(s):
        e = b.index(b"\0", shstr + s["name"]); return b[shstr + s["name"]:e].decode()

    byname = {nm(s): s for s in sh}
    for need in (".text", ".data", ".symtab", ".rela.text", ".rela.data"):
        if need not in byname:
            raise SystemExit("ABORT: no %s section (a stripped kernel cannot be relinked)" % need)
    text, data = byname[".text"], byname[".data"]
    data_idx = sh.index(data)
    symtab = byname[".symtab"]; strtab = sh[symtab["link"]]
    so, se = symtab["offset"], symtab["entsize"]; sn = symtab["size"] // se
    stroff = strtab["offset"]

    def symname(i):
        o = so + i * se; n = u32(b, o); e = b.index(b"\0", stroff + n); return b[stroff + n:e].decode()

    def defs(name):
        return [i for i in range(sn) if u16(b, so + i * se + 14) != 0 and symname(i) == name]

    def one(name, section=None):
        d = defs(name)
        if section is not None:
            d = [i for i in d if u16(b, so + i * se + 14) == section]
        if len(d) != 1:
            raise SystemExit("ABORT: %s: %d definitions, expected 1%s" % (
                name, len(d), " (were the kit's objects ld -r'd in?)" if not d else ""))
        return d[0]

    def rela_map(sec):
        """r_offset -> (entry file offset, symbol index, addend), for .rela.<sec>"""
        r = byname[".rela" + sec]; m = {}
        for i in range(r["size"] // r["entsize"]):
            o = r["offset"] + i * r["entsize"]
            if u32(b, o) in m:
                raise SystemExit("ABORT: two %s relocations at 0x%x" % (sec, u32(b, o)))
            m[u32(b, o)] = (o, u32(b, o + 4), u32(b, o + 8))
        return m

    rdata = rela_map(".data")

    def cstr(sec, addr):
        f = sec["offset"] + addr - sec["addr"]
        return bytes(b[f:b.index(b"\0", f)]).decode("latin-1")

    def rows(sym, n):
        """the n rows {pn, queue symbol, name string} of a table at sym"""
        base = u32(b, so + sym * se + 4)
        out = []
        for r in range(n):
            a = base + 12 * r
            pn = u32(b, data["offset"] + a - data["addr"])
            q = rdata.get(a + 4); s = rdata.get(a + 8)
            if q is None or s is None or q[1] & 0xff != R_68K_32 or s[1] & 0xff != R_68K_32:
                raise SystemExit("ABORT: %s row %d: queue/name words without R_68K_32 relocations"
                                 % (symname(sym), r))
            if q[2] != 0:
                raise SystemExit("ABORT: %s row %d: queue relocation has addend %d"
                                 % (symname(sym), r, q[2]))
            qs = symname(q[1] >> 8)
            ns = s[1] >> 8
            naddr = u32(b, so + ns * se + 4) + s[2]
            nx = u16(b, so + ns * se + 14)           # the stock names are .text literals
            nsec = sh[nx] if nx < len(sh) else None
            if nsec is None or nsec not in (text, data) or not nsec["addr"] <= naddr < nsec["addr"] + nsec["size"]:
                raise SystemExit("ABORT: %s row %d: name not in .text or .data" % (symname(sym), r))
            out.append((pn, qs, cstr(nsec, naddr)))
        return out

    # the stock table (local to sd.c) and ours
    stock = [i for i in defs(STOCK) if u16(b, so + i * se + 14) == data_idx]
    if len(stock) != 1:
        raise SystemExit("ABORT: %d definitions of %s in .data, expected 1" % (len(stock), STOCK))
    stock = stock[0]
    ssz = u32(b, so + stock * se + 8)
    if ssz not in (0, 36):
        raise SystemExit("ABORT: %s is %d bytes, not three 12-byte rows" % (STOCK, ssz))
    table = one(TABLE, data_idx)
    srows = rows(stock, 3)
    trows = rows(table, 5)
    for r, (s, t) in enumerate(zip(srows, trows)):
        if s != t:
            raise SystemExit("ABORT: scsicard row %d is %r, %s has %r" % (r, s, TABLE, t))
    if tuple(t[1] for t in trows) != QUEUES or tuple(t[0] for t in trows[3:]) != PRODUCTS:
        raise SystemExit("ABORT: %s rows %r, expected the queues %r and products %r"
                         % (TABLE, trows, QUEUES, PRODUCTS))
    for q in QUEUES:
        one(q)

    # 1. the lea relocation: exactly one .rela.text entry names scsicard or the table
    rtext = byname[".rela.text"]
    hits = []
    other = [i for n in OTHER for i in defs(n)]     # another kit's table, refused below
    for i in range(rtext["size"] // rtext["entsize"]):
        o = rtext["offset"] + i * rtext["entsize"]
        s = u32(b, o + 4) >> 8
        if s in [stock, table] + other:
            hits.append((o, u32(b, o), u32(b, o + 4), u32(b, o + 8)))
    if len(hits) != 1:
        raise SystemExit("ABORT: %d .rela.text relocations name %s/%s, expected 1 (init's lea)"
                         % (len(hits), STOCK, TABLE))
    ro, roff, rinfo, radd = hits[0]
    if rinfo & 0xff != R_68K_32 or radd != 0:
        raise SystemExit("ABORT: the scsicard relocation is type %d addend %d, expected R_68K_32 + 0"
                         % (rinfo & 0xff, radd))
    tf = text["offset"] + roff - text["addr"]
    if u16(b, tf - 2) != LEA_A3:
        raise SystemExit("ABORT: the scsicard relocation @0x%x is not the operand of lea ...,%%a3 "
                         "(opcode 0x%04x)" % (roff, u16(b, tf - 2)))
    cur = symname(rinfo >> 8)
    if cur in OTHER:
        raise SystemExit("ABORT: sd.c's controller table is %s already (another kit's): the "
                         "Buddha kit and that kit cannot be linked into one kernel yet" % cur)

    # 2. the loop bound, after the lea
    win = bytes(b[tf:tf + 0x80])
    old_at = [i for i in range(len(win) - len(LOOP) + 1) if win[i:i + len(LOOP)] == LOOP]
    new_at = [i for i in range(len(win) - len(LOOP) + 1) if win[i:i + len(LOOP)] == LOOP_NEW]
    if len(old_at) + len(new_at) != 1:
        raise SystemExit("ABORT: init()'s loop bound found %d times (bound 2) and %d times (bound 4) "
                         "after the lea @0x%x, expected once" % (len(old_at), len(new_at), roff))

    if cur == TABLE and new_at:
        print("  [skip] scsicard already -> %s, loop bound 4" % TABLE)
        return
    if cur != STOCK or not old_at:
        raise SystemExit("ABORT: half registered: lea -> %s, loop bound %d" % (cur, 2 if old_at else 4))
    struct.pack_into(">I", b, ro + 4, (table << 8) | R_68K_32)
    bo = tf + old_at[0] + BOUND
    b[bo] = LOOP_NEW[BOUND]
    print("  [ok]   lea scsicard @0x%x -> %s (0x%x)" % (roff, TABLE, u32(b, so + table * se + 4)))
    print("  [ok]   loop bound @0x%x moveq #2 -> #4 (five rows)" % (roff + old_at[0] + 2))
    for r, (pn, q, n) in enumerate(trows):
        print("         row %d  0x%08x  %-11s %s" % (r, pn, q, n))
    open(kernel, "wb").write(b)
    print("Buddha and Catweasel Z-II IDE registered (sd.c scsicard rows 3-4, budqueue, cwqueue) "
          "-> %s" % kernel)


main()
