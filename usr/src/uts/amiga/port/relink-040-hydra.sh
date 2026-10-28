#!/bin/sh
# relink-040-hydra.sh -- add the Hydra Systems AmigaNet (NE2000, Zorro II)
# STREAMS driver to a 68040/68060 kernel.  Usage:
#   HYDRA_SRC=<dir with hydra.c hydra.h hydrauser.h ne2000.h> sh relink-040-hydra.sh IN OUT
# The driver is cross-compiled with -DHYDRA_AUTOCON_ONLY: boards are found only
# through the AutoConfig table, never by reading raw Zorro II addresses (on a
# Z3660 machine those hold other boards, or nothing -- a bus error).  It is
# wired in at run time by src/parinit_hydra.s (cdevsw[47], int2_tbl[4] chain).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/tools/config-load.sh"
. "$HERE/tools/build-step.sh"
IN="$1"; OUT="$2"
[ -f "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 base output"; exit 1; }
SRC="${HYDRA_SRC:-$AMIX_ROOT/usr/sys/amiga/driver/hydra}"
[ -f "$SRC/hydra.c" ] || { echo "ERROR: hydra.c not found in $SRC (set HYDRA_SRC)"; exit 1; }
NM=m68k-linux-gnu-nm
$NM "$IN" | grep -qE " T hydrainit$" && { echo "[FAIL] base already has hydra"; exit 1; }
PAR=$($NM "$IN" | awk '$2=="T" && $3=="parinit"{print $1}')
[ -n "$PAR" ] || { echo "[FAIL] no parinit in base"; exit 1; }
echo "[*] base: $(basename "$IN"), parinit @0x$PAR"
sh "$HERE/src/mk_modelb_sysroot.sh" | sed 's/^/      /'
AMIX_SYSROOT="$HERE/build/sysroot-modelb"; export AMIX_SYSROOT
B="$HERE/build/hydrasrc"; rm -rf "$B"; mkdir -p "$B"
cp "$SRC/hydra.c" "$SRC/hydra.h" "$SRC/hydrauser.h" "$SRC/ne2000.h" "$B/"
python3 - "$B/hydra.c" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
# the raw probe: from the comment line of probe method 2 (the direct PROM
# probe) up to the line that reports no board found -- short markers
a = s.rfind("\n", 0, s.find("/* Method 2: Direct PROM")) + 1
b = s.rfind("\n", 0, s.find('"hydra: no board found"')) + 1
if a < 0 or b < 0 or b < a or "HYDRA_AUTOCON_ONLY" in s[:a]:
    sys.exit("[FAIL] hydra.c: raw-probe block not found as expected")
s = s[:a] + "#ifndef HYDRA_AUTOCON_ONLY\n" + s[a:b] + "#endif /* HYDRA_AUTOCON_ONLY */\n" + s[b:]
open(p, "w").write(s)
print("      [ok] raw Zorro II probes (methods 2, 3) under #ifndef HYDRA_AUTOCON_ONLY")
PY
CF=$(echo "$AMIX_KERNEL_CFLAGS" | sed 's/-m68020/-m68040/')
echo "[*] cross-compiling hydra.c"
(cd "$B" && m68k-cbm-sysv4-gcc $CF -DHYDRA_AUTOCON_ONLY -I. -c hydra.c -o "$HERE/build/hydra_040.o")
sh "$HERE/src/check_page_geometry.sh" "$HERE/build/hydra_040.o" | sed 's/^/      /'
if m68k-linux-gnu-objdump -d "$HERE/build/hydra_040.o" | grep -E "(mul|div)[su]l[.a-z]*\s+[^,]*,%d[0-7]:%d[0-7]"; then
	echo "[FAIL] hydra_040.o has 64-bit mul/div forms (060 vector 61)"; exit 1; fi
SR=$(m68k-linux-gnu-objdump -d "$HERE/build/hydra_040.o" | grep -cE ',%sr$')
[ "$SR" -gt 0 ] || { echo "[FAIL] hydra_040.o writes %sr nowhere: spl()/splx() lost (-Dvolatile=__volatile__ missing?)"; exit 1; }
echo "[OK] no 64-bit mul/div forms; $SR SR writes (spl/splx present)"
# the run-time wiring may only take an empty cdevsw slot and the A2065 entry
python3 - "$IN" <<'PY'
import struct, sys
d = open(sys.argv[1], "rb").read()
sh = struct.unpack_from(">I", d, 32)[0]; n = struct.unpack_from(">H", d, 48)[0]; si = struct.unpack_from(">H", d, 50)[0]
secs = [struct.unpack_from(">IIIIIIIIII", d, sh + i * 40) for i in range(n)]
nm = lambda s: d[secs[si][4] + s[0]:].split(b"\0")[0].decode()
sym = [s for s in secs if s[1] == 2][0]; st = secs[sym[6]]
syms = []
for k in range(sym[5] // 16):
    a, v, z, inf, o, x = struct.unpack_from(">IIIBBH", d, sym[4] + k * 16)
    syms.append((d[st[4] + a:].split(b"\0")[0].decode(), v, x))
def val(name):
    h = [(v, x) for n, v, x in syms if n == name and x not in (0,)]
    if len(h) != 1: sys.exit(f"[FAIL] {name}: {len(h)} definitions")
    return h[0]
data = [i for i, s in enumerate(secs) if nm(s) == ".data"][0]
rela = [s for s in secs if s[1] == 4 and s[7] == data][0]
relocs = {}
for k in range(rela[5] // 12):
    off, info, add = struct.unpack_from(">IIi", d, rela[4] + k * 12)
    relocs[off] = (syms[info >> 8][0], add)
cd, x = val("cdevsw"); assert x == data
slot = cd + 47 * 52
dstr = slot + 44
if dstr in relocs or struct.unpack_from(">I", d, secs[data][4] + dstr)[0]:
    sys.exit("[FAIL] cdevsw[47].d_str is not empty: %r" % (relocs.get(dstr),))
fields = [relocs.get(slot + 4 * i, ("-", 0))[0] for i in range(6)]
if any(f not in ("nodev", "-") for f in fields):
    sys.exit("[FAIL] cdevsw[47] is in use: %s" % fields)
it, x = val("int2_tbl"); assert x == data
if relocs.get(it + 16, ("", 0))[0] != "aenintr":
    sys.exit("[FAIL] int2_tbl[4] is %r, not aenintr" % (relocs.get(it + 16),))
print("      [ok] cdevsw[47] empty (d_open..d_mmap = %s); int2_tbl[4] = aenintr" % fields[0])
PY
m68k-cbm-sysv4-gcc -m68040 -c "$HERE/src/parinit_hydra.s" -o "$HERE/build/parinit_hydra.o"
STAGE="$HERE/build/unix-stage-hydra"; cp "$IN" "$STAGE"
m68k-linux-gnu-objcopy --weaken-symbol parinit \
	--add-symbol parinit_prev=.text:0x$PAR,function,global "$STAGE"
m68k-cbm-sysv4-ld -r -o "$OUT" "$STAGE" "$HERE/build/hydra_040.o" "$HERE/build/parinit_hydra.o"
for s in hydrainit hydraintr hydrainfo hydra_int2 parinit parinit_prev aenintr; do
	$NM "$OUT" | grep -qE " [A-Za-z] $s\$" || { echo "[FAIL] $s missing"; exit 1; }
done
[ "$($NM "$OUT" | grep -cE ' T parinit$')" -eq 1 ] || { echo "[FAIL] parinit not unique"; exit 1; }
LEAK=$($NM "$OUT" | awk '$1=="U" && $2!="edata" && $2!="end" && $2!="etext" {print $2}')
[ -z "$LEAK" ] || { echo "[FAIL] unresolved:"; echo "$LEAK"; exit 1; }
echo "[OK] symbols defined, none unresolved"
CONTIG=$(m68k-linux-gnu-readelf -SW "$OUT" | awk '{gsub(/[][]/,"")} $2==".text"{to=$5;ts=$6} $2==".data"{do_=$5} END{print to, ts, do_}')
set -- $CONTIG; set -- $(( 0x$1 + 0x$2 )) $(( 0x$3 ))
[ "$1" = "$2" ] && echo "[OK] text/data contiguous." || { echo "[FAIL] text/data NOT contiguous"; exit 1; }
DSZ=$(( 0x$(m68k-linux-gnu-readelf -SW "$OUT" | awk '{gsub(/[][]/,"")} $2==".data"{print $6}') ))
[ $((DSZ % 4)) -eq 0 ] && echo "[OK] .data 4-aligned" || { echo "[FAIL] .data misaligned"; exit 1; }
run_step 1 python3 "$HERE/src/check_relink_relocs.py" "$OUT"
python3 "$HERE/src/stamp_buildid.py" "$OUT"
echo "[OK] built $OUT"
echo "     node: the AMIX tree's /dev/hya0 (c 47) -- unchanged"
