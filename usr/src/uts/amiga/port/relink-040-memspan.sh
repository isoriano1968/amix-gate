#!/bin/sh
# relink-040-memspan.sh -- one page array over the memory regions below the
# kernel (src/memspan.s + tools/patch-memspan.py), step 2 of
# docs/MEMORY-REGIONS-PLAN.md: the A3000 motherboard RAM joins the VM.
# Needs the memregions stage in IN.  Usage: sh relink-040-memspan.sh IN OUT
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/tools/config-load.sh"
IN="$1"; OUT="$2"
[ -f "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 base output"; exit 1; }
NM=m68k-linux-gnu-nm
$NM "$IN" | awk '{print $NF}' | grep -qx memregion || { echo "[FAIL] base lacks the memregions stage"; exit 1; }
! $NM "$IN" | awk '{print $NF}' | grep -qx kvm_span_tail || { echo "[FAIL] base already has memspan"; exit 1; }
m68k-cbm-sysv4-gcc -m68040 -c "$HERE/src/memspan.s" -o "$HERE/build/memspan.o"
cp "$IN" "$HERE/build/unix-stage-memspan"
m68k-linux-gnu-objcopy --globalize-symbol kvm_init "$HERE/build/unix-stage-memspan"
m68k-cbm-sysv4-ld -r -o "$HERE/build/unix-stage-memspan.o" "$HERE/build/unix-stage-memspan" "$HERE/build/memspan.o"
LEAK=$($NM "$HERE/build/unix-stage-memspan.o" | awk '$1=="U" && $2!="edata" && $2!="end" && $2!="etext" {print $2}')
[ -z "$LEAK" ] || { echo "[FAIL] unresolved symbols:"; echo "$LEAK"; exit 1; }
python3 "$HERE/tools/patch-memspan.py" "$HERE/build/unix-stage-memspan.o" "$OUT"
python3 "$HERE/src/check_relink_relocs.py" "$OUT"
echo "[OK] built $OUT"
