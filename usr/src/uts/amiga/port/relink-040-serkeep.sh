#!/bin/sh
# relink-040-serkeep.sh -- mirror the kernel console onto the Amiga serial
# port (src/serdbg_keepbaud.s), keeping the speed the boot loader set.
# Usage:
#   sh relink-040-serkeep.sh IN OUT
# conputc is weakened, and the object's conputc (-> serdbg_putc, which then
# calls the stock coputc for the screen) takes its place.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/tools/config-load.sh"
IN="$1"; OUT="$2"
[ -f "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 base output"; exit 1; }
NM=m68k-linux-gnu-nm
[ "$($NM "$IN" | grep -cE ' D conputc$')" -eq 1 ] || { echo "[FAIL] base: conputc not a unique global"; exit 1; }
$NM "$IN" | grep -qE ' T serdbg_putc$' && { echo "[FAIL] base already mirrors the console"; exit 1; }
$NM "$IN" | grep -qE ' T coputc$' || { echo "[FAIL] base lacks coputc"; exit 1; }
echo "[*] base: $(basename "$IN")"
m68k-cbm-sysv4-gcc -c "$HERE/src/serdbg_keepbaud.s" -o "$HERE/build/serdbg_keepbaud.o"
STAGE="$HERE/build/unix-stage-serkeep"; cp "$IN" "$STAGE"
m68k-linux-gnu-objcopy --weaken-symbol conputc "$STAGE"
m68k-cbm-sysv4-ld -r -o "$OUT" "$STAGE" "$HERE/build/serdbg_keepbaud.o"
[ "$($NM "$OUT" | grep -cE ' D conputc$')" -eq 1 ] || { echo "[FAIL] conputc: not exactly one strong definition"; exit 1; }
LEAK=$($NM "$OUT" | awk '$1=="U" && $2!="edata" && $2!="end" && $2!="etext" {print $2}')
[ -z "$LEAK" ] || { echo "[FAIL] unresolved:"; echo "$LEAK"; exit 1; }
echo "[OK] built $OUT"
