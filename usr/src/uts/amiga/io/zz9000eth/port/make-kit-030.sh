#!/bin/sh
# make-kit-030.sh [OUTDIR] -- the AMIX Installer's zz9000eth kit for STOCK
# 68030 kernels: the MNT ZZ9000's ethernet port as the STREAMS DLPI driver
# zz9000ethinfo, /dev/zz0 (major 52), interface zz0.  OUTDIR defaults to kit/
# beside this script (gitignored).
#
#   zz9000eth_030.o         ../src/zz9000eth.c for the stock 2 KB-page kernel
#   zz9000eth_pad.o         pads the kernel's .data back to a multiple of 4
#   patch_zz9000eth_cdevsw.py  cdevsw[52].d_str -> zz9000ethinfo (STREAMS: the
#                           slot's d_xhalt relocation moved to d_str)
#   patch_io_init.py        (unused by this kit: no init; as in every kit)
#   check_relink_relocs.py  the loader's relocation check
#   KIT.info, KIT.sha256
#
# No init, no interrupt: the driver finds the board (AutoConfig MNT 0x6d6e,
# product 3; product 4 too) at the first open of /dev/zz0 and polls its
# receive backlog from a clock callout; without a ZZ9000 it touches nothing
# and says nothing.  It shares the board with the zz9000 kit (graphics,
# major 49) without overlap: registers 0x04/0x80..0x88 and the windows
# 0x2000..0x9FFF are this driver's, the rest the graphics driver's.
#
# Kernel code is compiled with the gcc 2.7.2.3 cross compiler only.  Needs
# AMIX_ROOT (an AMIX 2.1c tree with usr/include and usr/sys) and
# m68k-cbm-sysv4-gcc (developer/amix/gcc-27) on PATH.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../../../port" && pwd)
SRC=$(cd "$HERE/../src" && pwd)
. "$PORT/tools/kit-lib.sh"
kit_env
OUT=${1:-$HERE/kit}
VERSION=0.1

B="$HERE/build/zz9000ethkit"; rm -rf "$B"; mkdir -p "$B"
cp "$SRC/zz9000eth.c" "$SRC/zz9000eth.h" "$SRC/zz9000ethuser.h" "$B/"
(cd "$B" && m68k-cbm-sysv4-gcc $AMIX_KERNEL_CFLAGS -I. -c zz9000eth.c -o zz9000eth_030.o)
kit_check_030 "$B/zz9000eth_030.o"
U=$($NM "$B/zz9000eth_030.o" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
WANT="OTHERQ RD WR allocb autocon bcmp bcopy bzero canput cmn_err flushq freemsg getmajor getminor getq ifstats lbolt makedevice memcpy panicstr putbq putnext putq qreply sptalloc sptfree timeout unlinkb untimeout "
[ "$U" = "$WANT" ] || { echo "[FAIL] zz9000eth_030.o imports: $U"; exit 1; }
echo "[OK] imports only what the stock kernel has"
kit_pad "$B/zz9000eth_030.o" "$B/zz9000eth_pad.o"

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/zz9000eth_030.o" "$B/zz9000eth_pad.o" "$HERE/src/patch_zz9000eth_cdevsw.py" \
	"$KIT_IOINIT" "$KIT_RELOCS" "$OUT/"
kit_info "$OUT" id=zz9000eth version=$VERSION major=52 node=/dev/zz0 line=030 init= intr= \
	"entries=zz9000ethinfo" streams=yes
kit_sums "$OUT" zz9000eth_030.o zz9000eth_pad.o patch_zz9000eth_cdevsw.py \
	patch_io_init.py check_relink_relocs.py KIT.info
rm -rf "$B"
echo "[OK] zz9000eth kit $VERSION in $OUT"
cat "$OUT/KIT.info" "$OUT/KIT.sha256"
