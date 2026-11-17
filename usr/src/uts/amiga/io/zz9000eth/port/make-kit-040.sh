#!/bin/sh
# make-kit-040.sh [OUTDIR] -- the zz9000eth kit for the 68040/68060 PORT
# kernels (Model B, 4 KB pages): /dev/zz0 (major 52) for a ZZ9000 in a machine
# with a 68040/68060 (an A3000/A4000 with an accelerator).  OUTDIR defaults to
# kit-040/ beside this script (gitignored).
#
#   zz9000eth_040.o         ../src/zz9000eth.c, -m68040 against the Model-B
#                           headers, -DZZ9000ETH_KVMAP: a Zorro II board is
#                           reached through the identity map (cache-inhibited
#                           by DTT0), a Zorro III one mapped noncacheable
#                           through dev_kvmap, here named zz9000eth_kvmap
#   zz9000eth_kvmap.o       port/src/devkvmap040.s under those names
#   zz9000eth_pad.o         pads the kernel's .data back to a multiple of 4
#   patch_zz9000eth_cdevsw.py, patch_io_init.py, check_relink_relocs.py,
#   KIT.info (line=040), KIT.sha256
#
# dev_kvmap is privately named for the reason in io/z3660net/port/
# make-kit-040.sh: the va2000 and z3660eth kits and kernels such as m6-msp
# carry their own, and any combination must link once.
#
# Kernel code is compiled with the gcc 2.7.2.3 cross compiler only.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../../../port" && pwd)
SRC=$(cd "$HERE/../src" && pwd)
. "$PORT/tools/kit-lib.sh"
kit_env
OUT=${1:-$HERE/kit-040}
VERSION=0.1

B="$HERE/build/zz9000ethkit040"; rm -rf "$B"; mkdir -p "$B/src"
cp "$SRC/zz9000eth.c" "$SRC/zz9000eth.h" "$SRC/zz9000ethuser.h" "$B/src/"
kit_modelb_sysroot "$B"
(cd "$B/src" && AMIX_SYSROOT="$KIT_SYSROOT" m68k-cbm-sysv4-gcc $KIT_CF040 -DZZ9000ETH_KVMAP \
	-Ddev_kvmap=zz9000eth_kvmap -Ddev_kvunmap=zz9000eth_kvunmap -I. \
	-c zz9000eth.c -o "$B/zz9000eth_040.o")
kit_check_040 "$B/zz9000eth_040.o"
U=$($NM "$B/zz9000eth_040.o" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
WANT="OTHERQ RD WR allocb autocon bcmp bcopy bzero canput cmn_err cputype flushq freemsg getmajor getminor getq ifstats lbolt makedevice memcpy panicstr putbq putnext putq qreply sptfree timeout unlinkb untimeout zz9000eth_kvmap zz9000eth_kvunmap "
[ "$U" = "$WANT" ] || { echo "[FAIL] zz9000eth_040.o imports: $U"; exit 1; }
echo "[OK] imports: $U(-DZZ9000ETH_KVMAP took)"
m68k-cbm-sysv4-gcc -m68040 -c "$PORT/src/devkvmap040.s" -o "$B/devkvmap040.o"
$OBJCOPY --redefine-sym dev_kvmap=zz9000eth_kvmap --redefine-sym dev_kvunmap=zz9000eth_kvunmap \
	"$B/devkvmap040.o" "$B/zz9000eth_kvmap.o"
D=$($NM "$B/zz9000eth_kvmap.o" | awk '$2=="T"{print $3}' | LC_ALL=C sort | tr '\n' ' ')
[ "$D" = "zz9000eth_kvmap zz9000eth_kvunmap " ] || { echo "[FAIL] zz9000eth_kvmap.o defines: $D"; exit 1; }
kit_pad "$B/zz9000eth_040.o" "$B/zz9000eth_kvmap.o" "$B/zz9000eth_pad.o"

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/zz9000eth_040.o" "$B/zz9000eth_kvmap.o" "$B/zz9000eth_pad.o" "$HERE/src/patch_zz9000eth_cdevsw.py" \
	"$KIT_IOINIT" "$KIT_RELOCS" "$OUT/"
kit_info "$OUT" id=zz9000eth version=$VERSION major=52 node=/dev/zz0 line=040 init= intr= \
	"entries=zz9000ethinfo zz9000eth_kvmap zz9000eth_kvunmap" streams=yes
kit_sums "$OUT" zz9000eth_040.o zz9000eth_kvmap.o zz9000eth_pad.o patch_zz9000eth_cdevsw.py \
	patch_io_init.py check_relink_relocs.py KIT.info
rm -rf "$B"
echo "[OK] zz9000eth kit $VERSION (040) in $OUT"
cat "$OUT/KIT.info" "$OUT/KIT.sha256"
