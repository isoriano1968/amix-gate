#!/usr/local/bin/python3
"""zenintr.py [0|6] -- the Z3660 ethernet driver's INT6 receive switch in the
running kernel (z3660eth_intr, built with -DZ3660ETH_STATIC_INTR): with no
argument, show it and its counters; 6 turns INT6 receive on at the next poll
tick, 0 back to polling.  Needs root (writes /dev/kmem).  The kernel file is
found in /stand by the build id in UNAME_A (uname -a, passed by the caller:
this Python cannot run subprocesses); addresses as libamixstat finds them."""
import os, re, struct, sys

BASE = 0x08000000
WANT = ["z3660eth_intr", "z3660eth_intr_mode", "z3660eth_int6_n", "z3660eth_int2_n",
        "z3660eth_intrl2"]
OPTIONAL = ["z3660eth_rx_skipped", "z3660eth_rx_holds"]	# newer kernels

m = re.search(r"\b680[46]0(-\d{6}-\w+)", os.environ.get("UNAME_A", ""))
if not m:
    sys.exit("zenintr: UNAME_A=`uname -a` names no build id")
rx = re.compile(rb"680[2346]0" + re.escape(m.group(1).encode()) + rb"\b")
d = None
for n in sorted(os.listdir("/stand")):
    p = "/stand/" + n
    if os.path.isfile(p) and os.path.getsize(p) > 500000:
        b = open(p, "rb").read()
        if b[:4] == b"\x7fELF" and rx.search(b):
            d, kfile = b, p
            break
if d is None:
    sys.exit("zenintr: no kernel in /stand has build id " + m.group(1))

shoff, = struct.unpack_from(">I", d, 0x20)
ent, num, stri = struct.unpack_from(">HHH", d, 0x2E)
secs, run = [], 0
for i in range(num):
    name, typ, flags, addr, off, size, link, info, al, es = struct.unpack_from(">IIIIIIIIII", d, shoff + i * ent)
    vb = None
    if flags & 2:
        vb, run = run, run + size
    secs.append((name, typ, off, size, link, info, es, vb))
cs = lambda t, o: d[t + o:d.index(b"\0", t + o)].decode()
strt = secs[stri][2]
sym = [s for s in secs if s[1] == 2][0]
sst = secs[sym[4]][2]
names, idx = [], {}
for i in range(sym[3] // 16):
    no, val, size, inf, oth, shn = struct.unpack_from(">IIIBBH", d, sym[2] + i * 16)
    n = cs(sst, no) if no else ""
    names.append(n)
    if n in WANT or n in OPTIONAL:
        idx[n] = (shn, val)
km = open("/dev/kmem", "r+b", 0)


def rword(a):
    km.seek(a)
    return struct.unpack(">I", km.read(4))[0]


addr = {}
WANT += [n for n in OPTIONAL if n in idx]
for n in WANT:
    if n not in idx:
        sys.exit("zenintr: %s not in %s (not an INT6 kernel)" % (n, kfile))
    shn, val = idx[n]
    if shn and shn < 0xff00 and secs[shn][7] is not None:
        addr[n] = BASE + secs[shn][7] + val
        continue
    sites = []				# COMMON: its R_68K_32 sites in .text
    for s in secs:
        if s[1] != 4 or cs(strt, secs[s[5]][0]) != ".text":
            continue
        tb = secs[s[5]][7]
        for k in range(s[3] // 12):
            off, info, add = struct.unpack_from(">IIi", d, s[2] + k * 12)
            if info & 0xff == 1 and add == 0 and names[info >> 8] == n:
                sites.append(BASE + tb + off)
    vals = set(rword(a) for a in sites)
    if len(vals) != 1:
        sys.exit("zenintr: %s: %d sites, %d addresses" % (n, len(sites), len(vals)))
    addr[n] = vals.pop()

if len(sys.argv) > 1:
    v = int(sys.argv[1])
    if v not in (0, 6):
        sys.exit("zenintr: 0 or 6")
    km.seek(addr["z3660eth_intr"])
    km.write(struct.pack(">I", v))
    print("z3660eth_intr = %d (the next poll tick switches)" % v)
print("%s: " % kfile + ", ".join("%s %d" % (n[9:], rword(addr[n])) for n in WANT))
