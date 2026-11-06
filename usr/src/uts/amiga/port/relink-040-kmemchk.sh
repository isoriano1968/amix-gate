#!/bin/sh
# relink-040-kmemchk.sh -- debug guard on kmem_fast_alloc/kmem_fast_free
# (src/kmemchk.s): panic with a backtrace when an address below 0x40000000
# enters or leaves a fast free list.  For the 256 MB as_freelist hunt
# (2026-11-06); not for daily kernels.  Usage: sh relink-040-kmemchk.sh IN OUT
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/tools/config-load.sh"
IN="$1"; OUT="$2"
[ -f "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 base output"; exit 1; }
NM=m68k-linux-gnu-nm
A=$($NM "$IN" | awk '$3=="kmem_fast_alloc" && $2=="T" {print $1}')
F=$($NM "$IN" | awk '$3=="kmem_fast_free" && $2=="T" {print $1}')
[ -n "$A" ] && [ -n "$F" ] || { echo "[FAIL] base lacks kmem_fast_alloc/free"; exit 1; }
m68k-cbm-sysv4-gcc -m68040 -c "$HERE/src/kmemchk.s" -o "$HERE/build/kmemchk.o"
cp "$IN" "$HERE/build/unix-stage-kmemchk"
m68k-linux-gnu-objcopy --weaken-symbol kmem_fast_alloc --weaken-symbol kmem_fast_free \
	--add-symbol kmem_fast_alloc_orig=.text:0x$A,function,global \
	--add-symbol kmem_fast_free_orig=.text:0x$F,function,global \
	"$HERE/build/unix-stage-kmemchk"
m68k-cbm-sysv4-ld -r -o "$OUT" "$HERE/build/unix-stage-kmemchk" "$HERE/build/kmemchk.o"
LEAK=$($NM "$OUT" | awk '$1=="U" && $2!="edata" && $2!="end" && $2!="etext" {print $2}')
[ -z "$LEAK" ] || { echo "[FAIL] unresolved symbols:"; echo "$LEAK"; exit 1; }
python3 "$HERE/src/check_relink_relocs.py" "$OUT"
echo "[OK] built $OUT (kmem_fast_alloc_orig $A, kmem_fast_free_orig $F)"
