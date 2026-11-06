#!/bin/sh
# relink-040-ksegtbl.sh -- replace the 32-byte ksegtbl with an 8 KB one
# (src/ksegtbl.s): kseg() indexed past the stock table with any kvseg above
# 4 MB and overwrote as_freelist and its neighbours (the 256 MB panic in
# as_addseg, 2026-11-06).  Usage: sh relink-040-ksegtbl.sh IN OUT
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/tools/config-load.sh"
IN="$1"; OUT="$2"
[ -f "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 base output"; exit 1; }
NM=m68k-linux-gnu-nm
[ "$($NM "$IN" | awk '$3=="ksegtbl" {print $2}')" = "b" ] || { echo "[FAIL] base: ksegtbl is not the stock local bss table"; exit 1; }
m68k-cbm-sysv4-gcc -m68040 -c "$HERE/src/ksegtbl.s" -o "$HERE/build/ksegtbl.o"
cp "$IN" "$HERE/build/unix-stage-ksegtbl"
m68k-linux-gnu-objcopy --globalize-symbol ksegtbl --weaken-symbol ksegtbl "$HERE/build/unix-stage-ksegtbl"
m68k-cbm-sysv4-ld -r -o "$OUT" "$HERE/build/unix-stage-ksegtbl" "$HERE/build/ksegtbl.o"
[ "$($NM "$OUT" | awk '$3=="ksegtbl" {print $2}')" = "D" ] || { echo "[FAIL] ksegtbl did not move to the new table"; exit 1; }
LEAK=$($NM "$OUT" | awk '$1=="U" && $2!="edata" && $2!="end" && $2!="etext" {print $2}')
[ -z "$LEAK" ] || { echo "[FAIL] unresolved symbols:"; echo "$LEAK"; exit 1; }
python3 "$HERE/src/check_relink_relocs.py" "$OUT"
echo "[OK] built $OUT (ksegtbl 8192 bytes)"
