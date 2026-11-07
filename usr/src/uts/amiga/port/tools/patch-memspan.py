#!/usr/bin/env python3
"""patch-memspan.py IN OUT -- wire src/memspan.s into a linked kernel (step 2
of docs/MEMORY-REGIONS-PLAN.md).  IN must already contain memspan.o.

1. kvm_init+0x1ea (0x48e18, `movel %d1,%d5; addil #4095,%d5`): bra.l kvm_span_tail.
2. Every page-array range check `cmpl pages_base,%dN` directly followed by a
   bcs (the "not a VM page" exit) -> `bsr.l pfnchk_dN`, the bcs bytes kept.
   Not hat_free's Lf_A clamp (`bcs; movel pages_base,%d0`: a min()).
3. The page-table plausibility guards in hat_unload (Lhl_aok, Lhl_bok),
   krnxmemflt (Lkx_haverw) and i10rev (Lip_okbase) take `_start>>12` as the
   lowest RAM pfn; motherboard RAM lies below it: `movel #_start,%dN` ->
   `movel mspan_loaddr,%dN` (min of the lowest region and _start, set by
   mspan_build), its relocation retargeted from _start to mspan_loaddr.
Each replaced instruction's relocation is moved into mspan_sink, so the
loader's fixup no longer lands in the new bytes.  Every site is checked
against its expected bytes; the expected counts guard against a changed base.
"""
import struct, sys

EXPECT_RANGE = 37          # cmpl pages_base + bcs sites: 26 two-compare pairs + 11 with a reload between
EXPECT_LOWER = 4           # movel #_start guards


def die(msg):
    sys.exit("[FAIL] patch-memspan: " + msg)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    b = bytearray(open(src, "rb").read())
    if b[:4] != b"\x7fELF" or b[4] != 1 or b[5] != 2:
        die("not a big-endian ELF32")
    shoff, = struct.unpack_from(">I", b, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from(">HHH", b, 0x2e)
    secs = []
    for i in range(shnum):
        secs.append(struct.unpack_from(">IIIIIIIIII", b, shoff + i * shentsize))
    strsec = secs[shstrndx]

    def name(off, tab):
        e = b.index(b"\0", tab[4] + off)
        return b[tab[4] + off:e].decode()

    byname = {name(s[0], strsec): (i, s) for i, s in enumerate(secs)}
    ti, text = byname[".text"]
    toff = text[4]
    si, symtab = byname[".symtab"]
    strtab = secs[symtab[6]]
    syms = []
    for k in range(symtab[5] // 16):
        st_name, st_value, st_size, st_info, st_other, st_shndx = \
            struct.unpack_from(">IIIBBH", b, symtab[4] + 16 * k)
        syms.append((name(st_name, strtab), st_value, st_shndx, st_info))
    symidx = {}
    for k, (n, v, sh, inf) in enumerate(syms):
        symidx.setdefault(n, []).append(k)

    def text_sym(n):
        c = [syms[k][1] for k in symidx.get(n, []) if syms[k][2] == ti]
        if len(c) != 1:
            die(f"symbol {n}: {len(c)} definitions in .text")
        return c[0]

    rel = None
    for i, s in enumerate(secs):
        if s[1] == 4 and s[7] == ti:  # SHT_RELA for .text
            rel = s
    if rel is None:
        die("no .rela.text")
    nrel = rel[5] // 12
    relat = {}
    for k in range(nrel):
        r_off, r_info, r_add = struct.unpack_from(">IIi", b, rel[4] + 12 * k)
        relat.setdefault(r_off, []).append((k, r_info >> 8, r_add))

    def relsym(off):
        return [syms[s][0] for _, s, _ in relat.get(off, [])]

    sink = text_sym("mspan_sink")
    sink_used = [0]

    def sink_reloc(off):
        for k, s, a in relat.get(off, []):
            if sink_used[0] >= 64:
                die("mspan_sink full")
            new = sink + 4 * sink_used[0]
            sink_used[0] += 1
            struct.pack_into(">I", b, rel[4] + 12 * k, new)
        relat.pop(off, None)

    def w16(a):
        return struct.unpack_from(">H", b, toff + a)[0]

    # 1. kvm_init jump
    kvm = text_sym("kvm_init")
    tail = text_sym("kvm_span_tail")
    site = kvm + 0x1ea
    if bytes(b[toff + site:toff + site + 8]) != bytes.fromhex("2a0106850000 0fff".replace(" ", "")):
        die(f"kvm_init+0x1ea: unexpected bytes {b[toff+site:toff+site+8].hex()}")
    if relat.get(site + 2):
        die("kvm_init+0x1ea carries a relocation")
    struct.pack_into(">Hi", b, toff + site, 0x60FF, tail - (site + 2))
    print(f"[ok] kvm_init+0x1ea {site:#x}: bra.l kvm_span_tail {tail:#x}")

    # 2. range checks
    nrange = 0
    for off in sorted(list(relat)):
        if "pages_base" not in relsym(off):
            continue
        a = off - 2
        op = w16(a)
        if op & 0xF1FF != 0xB0B9:          # cmpl abs.l,%dN
            continue
        nxt = w16(a + 6)
        if nxt & 0xFF00 != 0x6500:         # bcs
            continue
        if w16(a + 10) & 0xF1FF == 0x2039 and relsym(a + 12) == ["pages_base"]:
            print(f"[--] {a:#x}: min(x, pages_base) clamp (hat_free Lf_A) left as is")
            continue
        reg = (op >> 9) & 7
        tgt = text_sym(f"pfnchk_d{reg}")
        sink_reloc(off)
        struct.pack_into(">Hi", b, toff + a, 0x61FF, tgt - (a + 2))
        nrange += 1
    print(f"[ok] {nrange} range checks -> bsr.l pfnchk_dN")
    if nrange != EXPECT_RANGE:
        die(f"expected {EXPECT_RANGE} range checks, found {nrange}")

    # 3. lower-bound guards: first `movel #_start,%dN` after each label
    dsec = byname[".data"][0]
    lk = [k for k in symidx.get("mspan_loaddr", []) if syms[k][2] == dsec]
    if len(lk) != 1:
        die("mspan_loaddr not defined once in .data")
    lk = lk[0]
    nlow = 0
    for lab in ("Lhl_aok", "Lhl_bok", "Lkx_haverw", "Lip_okbase"):
        base = text_sym(lab)
        for a in range(base, base + 0x60, 2):
            op = w16(a)
            if op & 0xF1FF == 0x203C and relsym(a + 2) == ["_start"]:
                reg = (op >> 9) & 7
                (k, s_, add), = relat[a + 2]
                if add != 0:
                    die(f"{lab}: _start relocation has addend {add}")
                r_off, r_info, r_add = struct.unpack_from(">IIi", b, rel[4] + 12 * k)
                struct.pack_into(">IIi", b, rel[4] + 12 * k, r_off, (lk << 8) | (r_info & 0xFF), 0)
                struct.pack_into(">H", b, toff + a, 0x2039 | (reg << 9))
                print(f"[ok] {lab} {a:#x}: movel #_start,%d{reg} -> movel mspan_loaddr,%d{reg}")
                nlow += 1
                break
        else:
            die(f"{lab}: no movel #_start within 0x60 bytes")
    if nlow != EXPECT_LOWER:
        die(f"expected {EXPECT_LOWER} lower-bound guards, found {nlow}")
    print(f"[ok] {sink_used[0]} relocations moved into mspan_sink")
    open(dst, "wb").write(b)


main()
