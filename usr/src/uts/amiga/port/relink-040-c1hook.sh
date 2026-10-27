#!/bin/sh
# relink-040-c1hook.sh -- add the RTG text-console hook (amix-z3660rtg
# integration/c1-console.txt) to a 68040/68060 kernel that already has the
# zz9000 driver (relink-040-zz9000kv.sh).  Usage:
#   sh relink-040-c1hook.sh IN OUT
# The stock scrcon_put is weakened and kept as scrcon_put_stock;
# src/scrcon_zz9k.s runs first and hands the displayed screen's output to
# zz9000consolewrite() while the console redirect is on.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/tools/config-load.sh"
. "$HERE/tools/build-step.sh"
IN="$1"; OUT="$2"
[ -f "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 base output"; exit 1; }
NM=m68k-linux-gnu-nm
$NM "$IN" | grep -qE " T zz9000consolewrite$" || { echo "[FAIL] base has no zz9000 driver"; exit 1; }
$NM "$IN" | grep -qE " T scrcon_put_stock$" && { echo "[FAIL] base already has the console hook"; exit 1; }
SCP=$($NM "$IN" | awk '$2=="T" && $3=="scrcon_put"{print $1}')
[ -n "$SCP" ] || { echo "[FAIL] no global scrcon_put in base"; exit 1; }
echo "[*] checking the stock scrcon_put at 0x$SCP"
python3 - "$IN" "$SCP" <<'PY'
import struct, sys, subprocess
path, scp = sys.argv[1], int(sys.argv[2], 16)
d = open(path, "rb").read()
shoff, = struct.unpack_from(">I", d, 32); shnum, = struct.unpack_from(">H", d, 48)
secs = [struct.unpack_from(">IIIIIIIIII", d, shoff + i * 40) for i in range(shnum)]
strs = secs[struct.unpack_from(">H", d, 50)[0]]
name = lambda s: d[strs[4] + s[0]:].split(b"\0")[0].decode()
text = [s for s in secs if name(s) == ".text"][0]
code = d[text[4] + scp: text[4] + scp + 32]
# linkw %fp,#-40; moveml; moveal %fp@(8),%a3 (cp); ...; moveal %a3@(8),%a2 (cp->screen)
if code[:2] != b"\x4e\x56" or b"\x26\x6e\x00\x08" not in code or b"\x24\x6b\x00\x08" not in code:
    sys.exit("[FAIL] stock scrcon_put prologue differs: %s" % code.hex())
print("      [ok] stock scrcon_put reads cp->screen at offset 8")
rel = subprocess.run(["m68k-linux-gnu-readelf", "-rW", path], capture_output=True, text=True).stdout
bysym = sum(1 for l in rel.splitlines() if l.split()[-3:-2] == ["scrcon_put"])
if not bysym:
    sys.exit("[FAIL] no symbol relocation against scrcon_put")
print("      [ok] %d references go through the scrcon_put symbol" % bysym)
PY
m68k-cbm-sysv4-gcc -m68040 -c "$HERE/src/scrcon_zz9k.s" -o "$HERE/build/scrcon_zz9k.o"
STAGE="$HERE/build/unix-stage-c1hook"; cp "$IN" "$STAGE"
m68k-linux-gnu-objcopy --weaken-symbol scrcon_put \
	--add-symbol scrcon_put_stock=.text:0x$SCP,function,global "$STAGE"
m68k-cbm-sysv4-ld -r -o "$OUT" "$STAGE" "$HERE/build/scrcon_zz9k.o"
for s in scrcon_put scrcon_put_stock zz9000consolewrite displayedscreen; do
	$NM "$OUT" | grep -qE " [A-Za-z] $s\$" || { echo "[FAIL] $s missing"; exit 1; }
done
[ "$($NM "$OUT" | grep -cE ' T scrcon_put$')" -eq 1 ] || { echo "[FAIL] scrcon_put not unique"; exit 1; }
[ "$($NM "$OUT" | awk '$3=="scrcon_put"{print $1}')" != "$SCP" ] || { echo "[FAIL] scrcon_put still resolves to the stock body"; exit 1; }
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
