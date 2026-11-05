#!/bin/sh
# relink-040-z3660intr.sh -- wire the Z3660 ethernet driver's INT6 receive
# into a kernel whose driver was built with it (relink-040-z3660.sh with
# Z3660ETH_CFLAGS=-DZ3660ETH_STATIC_INTR): p6int's CIA-B call and int2_tbl
# slot 4 go to the driver's wrappers, which call the handler that was there
# first (src/patch_z3660_intr.py); run it after relink-040-hydra.sh, which
# takes the same int2_tbl slot.  Nothing is linked in.  The driver stays
# polled until z3660eth_intr is set to 6 in /dev/kmem; 0 turns it back.
# Usage:
#   sh relink-040-z3660intr.sh IN OUT
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/tools/config-load.sh"
. "$HERE/tools/build-step.sh"
IN="$1"; OUT="$2"
[ -f "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 base output"; exit 1; }
NM=m68k-linux-gnu-nm
for s in p6int int2_tbl z3660eth_p6 z3660eth_p2 z3660eth_p6_next z3660eth_p2_next; do
	$NM "$IN" | grep -qE " [TtDdBC] $s\$" || { echo "[FAIL] base lacks $s (driver built without -DZ3660ETH_STATIC_INTR?)"; exit 1; }
done
echo "[*] base: $(basename "$IN")"
cp "$IN" "$OUT"
run_step 2 python3 "$HERE/src/patch_z3660_intr.py" "$OUT"
run_step 1 python3 "$HERE/src/check_relink_relocs.py" "$OUT"
python3 "$HERE/src/stamp_buildid.py" "$OUT"
echo "[OK] built $OUT"
