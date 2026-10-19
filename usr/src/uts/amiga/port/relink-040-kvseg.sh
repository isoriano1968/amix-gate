#!/bin/sh
# relink-040-kvseg.sh -- the base kernel with a 12 MB kvseg (src/kvseg040.s,
# src/patch_kvseg.py), for machines with more RAM than 4 MB of kvseg can index
# (a Z3660's 128 MB CPU RAM).  Usage: sh relink-040-kvseg.sh [IN] [OUT]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/tools/config-load.sh"
IN="${1:-$HERE/build/unix-040}"
OUT="${2:-$HERE/build/unix-040-kv12}"
[ -f "$IN" ] || { echo "ERROR: base kernel missing: $IN"; exit 1; }
m68k-cbm-sysv4-gcc -m68040 -c "$HERE/src/kvseg040.s" -o "$HERE/build/kvseg040.o"
# smsegs is a local data symbol in the stock kernel: make it visible to the clamp
cp "$IN" "$HERE/build/unix-stage-kvseg"
m68k-linux-gnu-objcopy --globalize-symbol smsegs "$HERE/build/unix-stage-kvseg"
m68k-cbm-sysv4-ld -r -o "$OUT" "$HERE/build/unix-stage-kvseg" "$HERE/build/kvseg040.o"
python3 "$HERE/src/patch_kvseg.py" "$OUT"
LEAK=$(m68k-linux-gnu-nm "$OUT" | awk '$1=="U" && $2!="edata" && $2!="end" && $2!="etext" {print $2}')
[ -z "$LEAK" ] || { echo "[FAIL] unresolved symbols:"; echo "$LEAK"; exit 1; }
python3 "$HERE/src/check_relink_relocs.py" "$OUT"
echo "[OK] built $OUT"
