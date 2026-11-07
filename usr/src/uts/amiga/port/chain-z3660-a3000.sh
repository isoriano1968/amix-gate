#!/bin/sh
# chain-z3660-a3000.sh PREFIX -- the A3000 + Z3660 (real 68060) kernel, as
# /stand/unix.z3 on the box since 2026-11-05 (68040-261105-x4):
#   z3660 (SCSI ZS-05, ethernet 1.0.0-7 with INT6 receive) -> zz9000kv (FB_NC,
#   MAP_BOARD) -> z3660intr (int2_tbl slot 4 for the ethernet drain alone: no
#   Hydra, the A2065 left uncalled) -> vt (virtual terminals) -> audio -> paula
#   -> stamp-rootcard card 1 unit 0 -> CACR a0c08000 -> random -> shm
#   -> ksegtbl (8 KB kseg table: the stock 32 bytes overflowed into
#      as_freelist with a kvseg above 4 MB; the 256 MB panic, 2026-11-06)
#   -> memregions (region table + pfn_valid, docs/MEMORY-REGIONS-PLAN.md step 1)
#   -> memspan (one page array over the regions below the kernel, step 2)
# From $BASE, default build/a3k-kv16-ser (relink-040-kvseg.sh KVSEG_MB=16 + relink-040-serkeep.sh
# on build/unix-040).  The driver sources come from the amix-z3660scsi and
# amix-z3660net checkouts (branches zs05-amigaux, eth-amigaux-1.0.0-7).
# Outputs build/z3l/PREFIX-<stage>; never touches the existing images.  The
# build-id block is per day: past it, stamp the last stage by hand.
set -e
P="$1"; [ -n "$P" ] || { echo "usage: $0 prefix"; exit 1; }
cd "$(dirname "$0")"
B=build
O=build/z3l
BASE="${BASE:-$B/a3k-kv16-ser}"
Z3660ETH_CFLAGS=-DZ3660ETH_STATIC_INTR sh relink-040-z3660.sh "$BASE" $O/$P-z3
ZZ9K_CFLAGS="-DZZ9K_FB_NC -DZZ9K_MAP_BOARD" sh relink-040-zz9000kv.sh $O/$P-z3 $O/$P-rtg
Z3660ETH_P2_ALONE=1 sh relink-040-z3660intr.sh $O/$P-rtg $O/$P-int
sh relink-040-vt.sh     $O/$P-int   $O/$P-con
sh relink-040-audio.sh  $O/$P-con   $O/$P-aud
sh relink-040-paula.sh  $O/$P-aud   $O/$P-paula2
python3 tools/stamp-rootcard.py $O/$P-paula2 $O/$P-paula2-c1u0x --card 1 --unit 0
python3 ../port-kernel/tools/patch-kernel-cacr.py $O/$P-paula2-c1u0x $O/$P-paula2-c1u0 a0c08000
sh relink-040-random.sh $O/$P-paula2-c1u0 $O/$P-rnd2
sh relink-040-shm.sh    $O/$P-rnd2 $O/$P-shm
sh relink-040-ksegtbl.sh $O/$P-shm $O/$P-ksg
sh relink-040-memregions.sh $O/$P-ksg $O/$P-mrg
sh relink-040-memspan.sh $O/$P-mrg $O/$P-msp
echo "[chain OK] $O/$P-msp"
