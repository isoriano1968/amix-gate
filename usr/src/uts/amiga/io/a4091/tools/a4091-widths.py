#!/usr/bin/env python3
"""a4091-widths.py -- access widths of every 53C710 register access in a compiled a4091 object.

The 53C710 on the A4091 takes 8- and 32-bit accesses only, never 16-bit, and its 32-bit
registers (DSA, TEMP, DBC, DSP, DSPS, SCRATCH) must be written as one longword. An earlier
build showed that -traditional once turned 32-bit MMIO reads into byte reads by erasing
`volatile`; this checks the compiled bytes, not the source.

Method: `objdump -dr`; a register loaded from the `siop` symbol (the current-card 53C710
view) is followed until it is overwritten; every `%aN@(d)` access through it is classified by
its mnemonic's size suffix.  (objdump prints the 16-bit displacement in decimal.)  Offsets are
the read side (0x00-0x3f) or the write shadow (0x40-0x7f).

Rules: no word access at all; 32-bit registers only as longwords; everything else only as
bytes.  Prints a table offset -> widths -> count, exits 1 on any violation.
usage: a4091-widths.py OBJECT [--objdump m68k-linux-gnu-objdump]
"""
import re
import subprocess
import sys

LONG_REGS = {0x10: "DSA", 0x1C: "TEMP", 0x24: "DBC", 0x2C: "DSP", 0x30: "DSPS", 0x34: "SCRATCH"}
NAMES = {0x00: "SIEN", 0x02: "SCNTL1", 0x03: "SCNTL0", 0x07: "SCID", 0x08: "SBCL",
         0x0C: "SSTAT2", 0x0D: "SSTAT1", 0x0E: "SSTAT0", 0x0F: "DSTAT", 0x16: "CTEST1",
         0x17: "CTEST0", 0x18: "CTEST7", 0x21: "CTEST8", 0x22: "ISTAT", 0x23: "DFIFO",
         0x38: "DCNTL", 0x39: "DWT", 0x3A: "DIEN", 0x3B: "DMODE"}
NAMES.update(LONG_REGS)

INSN_RE = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4}\s)+\s*(\S+)\s*(.*)$")
RELOC_RE = re.compile(r"^\s*[0-9a-f]+:\s+R_68K_\w+\s+(\S+)")
FUNC_RE = re.compile(r"^[0-9a-f]+ <([^>+]+)>:")
ACC_RE = re.compile(r"%(a[0-7])@\((-?\d+)\)")
SIZE = {"b": 8, "w": 16, "l": 32}


def size_of(mn):
    m = re.match(r"^(?:move|clr|or|and|eor|add|sub|cmp|tst|not|neg)[a-z]*?([bwl])$", mn)
    if m:
        return SIZE[m.group(1)]
    if mn in ("btst", "bset", "bclr", "bchg"):   # memory bit ops are byte ops
        return 8
    return None


def main():
    obj = sys.argv[1]
    od = "m68k-linux-gnu-objdump"
    if "--objdump" in sys.argv:
        od = sys.argv[sys.argv.index("--objdump") + 1]
    text = subprocess.run([od, "-dr", obj], capture_output=True, text=True,
                          env={"LC_ALL": "C", "PATH": "/usr/bin:/bin"}, check=True).stdout
    lines = text.splitlines()
    # `siop` is a file-static in .bss, so its relocations name the section, not the symbol:
    # accept both spellings, the section one computed from nm.
    targets = {"siop"}
    nm = od.replace("objdump", "nm")
    for ln in subprocess.run([nm, obj], capture_output=True, text=True, check=True).stdout.splitlines():
        p = ln.split()
        if len(p) == 3 and p[2] == "siop":
            sec = {"b": ".bss", "B": ".bss", "d": ".data", "D": ".data"}.get(p[1])
            if sec:
                targets.add("%s+0x%x" % (sec, int(p[0], 16)) if int(p[0], 16) else sec)
    table = {}
    bad = []
    func = "?"
    live = set()          # address registers currently holding `siop`
    pending = None        # register of a moveal whose relocation follows
    for ln in lines:
        f = FUNC_RE.match(ln)
        if f:
            func, live, pending = f.group(1), set(), None
            continue
        r = RELOC_RE.match(ln)
        if r:
            if pending and r.group(1) in targets:
                live.add(pending)
            pending = None
            continue
        m = INSN_RE.match(ln)
        if not m:
            continue
        pending = None
        mn, ops = m.group(2), m.group(3)
        for a in ACC_RE.finditer(ops):
            if a.group(1) in live:
                off = int(a.group(2))
                w = size_of(mn)
                reg = off & 0x3F if off >= 0x40 else off
                side = "wr" if off >= 0x40 else "rd"
                key = (reg, side)
                table.setdefault(key, {}).setdefault(w, []).append("%s+%s" % (func, m.group(1)))
                if w is None or w == 16:
                    bad.append("%s @%s: %s %s -- width %s" % (func, m.group(1), mn, ops, w))
                elif reg in LONG_REGS and w != 32:
                    bad.append("%s @%s: %s %s -- %s is 32-bit, accessed as %d" %
                               (func, m.group(1), mn, ops, LONG_REGS[reg], w))
                elif reg not in LONG_REGS and w != 8:
                    bad.append("%s @%s: %s %s -- %s is a byte register, accessed as %d" %
                               (func, m.group(1), mn, ops, NAMES.get(reg, hex(reg)), w))
        dst = re.search(r",%(a[0-7])$", ops)
        if dst:
            live.discard(dst.group(1))
            if mn in ("moveal", "movel") and re.match(r"^0 <", ops):
                pending = dst.group(1)
    print("%-8s %-3s %-5s %s" % ("reg", "rd/wr", "width", "count  (sites)"))
    for (reg, side) in sorted(table):
        for w, sites in sorted(table[(reg, side)].items(), key=lambda x: (x[0] or 0)):
            print("0x%02x %-7s %-3s %-5s %d  (%s)" % (reg, NAMES.get(reg, "?"), side, w, len(sites),
                                                     ", ".join(sorted(set(s.split("+")[0] for s in sites)))))
    n = sum(len(s) for d in table.values() for s in d.values())
    if n == 0:
        print("FAIL: no access through `siop` found -- not an a4091 object, or the tracker is blind")
        return 1
    if bad:
        print("FAIL: %d access(es) of the wrong width:" % len(bad))
        for b in bad:
            print("  " + b)
        return 1
    print("OK: %d 53C710 accesses through siop; bytes on byte registers, longwords on 32-bit ones, no word access"
          % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
