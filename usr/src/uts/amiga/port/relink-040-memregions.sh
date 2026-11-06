#!/bin/sh
# relink-040-memregions.sh -- the memory region table (src/memregions.s), step 1
# of docs/MEMORY-REGIONS-PLAN.md: record every bootinfo.memory[] region and
# export pfn_valid(); prints the table after startup().  No change to how the
# kernel uses memory.  Usage: sh relink-040-memregions.sh IN OUT
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/tools/config-load.sh"
IN="$1"; OUT="$2"
[ -f "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 base output"; exit 1; }
NM=m68k-linux-gnu-nm
M=$($NM "$IN" | awk '$3=="mlsetup" && $2=="T" {print $1}')
S=$($NM "$IN" | awk '$3=="startup" && $2=="T" {print $1}')
[ -n "$M" ] && [ -n "$S" ] || { echo "[FAIL] base lacks mlsetup/startup"; exit 1; }
for s in mlsetup_orig startup_orig pfn_valid memregion; do
	! $NM "$IN" | awk '{print $NF}' | grep -qx "$s" || { echo "[FAIL] base already has $s"; exit 1; }
done
m68k-cbm-sysv4-gcc -m68040 -c "$HERE/src/memregions.s" -o "$HERE/build/memregions.o"
cp "$IN" "$HERE/build/unix-stage-memregions"
m68k-linux-gnu-objcopy --weaken-symbol mlsetup --weaken-symbol startup \
	--add-symbol mlsetup_orig=.text:0x$M,function,global \
	--add-symbol startup_orig=.text:0x$S,function,global \
	"$HERE/build/unix-stage-memregions"
m68k-cbm-sysv4-ld -r -o "$OUT" "$HERE/build/unix-stage-memregions" "$HERE/build/memregions.o"
for s in mlsetup startup; do
	[ "$($NM "$OUT" | awk -v s=$s '$3==s {print $1}')" != "$($NM "$IN" | awk -v s=$s '$3==s {print $1}')" ] \
		|| { echo "[FAIL] $s still resolves to the stock body"; exit 1; }
done
LEAK=$($NM "$OUT" | awk '$1=="U" && $2!="edata" && $2!="end" && $2!="etext" {print $2}')
[ -z "$LEAK" ] || { echo "[FAIL] unresolved symbols:"; echo "$LEAK"; exit 1; }
python3 "$HERE/src/check_relink_relocs.py" "$OUT"
echo "[OK] built $OUT (mlsetup_orig $M, startup_orig $S)"
