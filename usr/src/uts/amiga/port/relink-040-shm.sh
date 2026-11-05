#!/bin/sh
# relink-040-shm.sh -- raise a 68040/68060 kernel's System V shared memory
# limits (src/patch_shminfo.py): SHMMAX 327680 -> 16 MB, so a 1920x1080
# 32-bit picture fits one MIT-SHM segment, and SHMSEG 6 -> 32 segments per
# process.  A data patch only; nothing is linked in.  Usage:
#   sh relink-040-shm.sh IN OUT
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/tools/config-load.sh"
. "$HERE/tools/build-step.sh"
IN="$1"; OUT="$2"
[ -f "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 base output"; exit 1; }
NM=m68k-linux-gnu-nm
for s in shminfo shmget shmat; do
	$NM "$IN" | grep -qE " [TtDdBC] $s\$" || { echo "[FAIL] base lacks $s"; exit 1; }
done
echo "[*] base: $(basename "$IN")"
cp "$IN" "$OUT"
run_step 2 python3 "$HERE/src/patch_shminfo.py" "$OUT"
run_step 1 python3 "$HERE/src/check_relink_relocs.py" "$OUT"
python3 "$HERE/src/stamp_buildid.py" "$OUT"
echo "[OK] built $OUT"
