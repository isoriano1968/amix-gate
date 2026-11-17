#!/bin/sh
# make-kit-030.sh [OUTDIR] -- the AMIX Installer's xsurf100 kit for STOCK
# 68030 kernels: the Individual Computers X-Surf 100 (ASIX AX88796B, Zorro
# II/III) as the STREAMS DLPI driver xsurf100info, /dev/xsf0 (major 56),
# interface xsf0.  OUTDIR defaults to kit/ beside this script (gitignored).
#
#   xsurf100_030.o          ../xsurf100.c (the X-Surf backend, which includes
#                           the shared DP8390 core ../../dp8390/dp8390.c) for
#                           the stock 2 KB-page kernel; a Zorro III board
#                           above the identity-mapped 1 GB through sptalloc
#   xsurf100_pad.o          pads the kernel's .data back to a multiple of 4
#   patch_xsurf100_cdevsw.py  cdevsw[56].d_str -> xsurf100info (STREAMS: the
#                           slot's d_xhalt relocation moved to d_str, by symbol)
#   patch_int2.py           int2_tbl's aenintr entry -> the installer's level-2
#                           chain amixdrv_int2: aenintr, then xsurf100intr
#                           (KIT.info intr=; port/src)
#   patch_io_init.py        io_init[0] parinit -> the installer's driver chain
#                           amixdrv_init, which calls xsurf100init (port/src)
#   check_relink_relocs.py  the loader's relocation check
#   KIT.info, KIT.sha256
#
# xsurf100init only asks AutoConfig (4626/100) for boards and says nothing;
# the board is mapped and started at the first open of /dev/xsf0.
#
# Kernel code is compiled with the gcc 2.7.2.3 cross compiler only.  Needs
# AMIX_ROOT (an AMIX 2.1c tree with usr/include and usr/sys) and
# m68k-cbm-sysv4-gcc (developer/amix/gcc-27) on PATH.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../../../port" && pwd)
SRC=$(cd "$HERE/.." && pwd)
CORE=$(cd "$HERE/../../dp8390" && pwd)
. "$PORT/tools/kit-lib.sh"
kit_env
OUT=${1:-$HERE/kit}
VERSION=0.2

B="$HERE/build/xsurf100kit"; rm -rf "$B"; mkdir -p "$B"
cp "$SRC/xsurf100.c" "$SRC/xsurf100user.h" "$CORE/dp8390.c" "$CORE/dp8390.h" "$CORE/dp8390reg.h" "$B/"
CF="$AMIX_KERNEL_CFLAGS -Dsigned=__signed__"
(cd "$B" && m68k-cbm-sysv4-gcc $CF -I. -c xsurf100.c -o xsurf100_030.o)
kit_check_030 "$B/xsurf100_030.o"
U=$($NM "$B/xsurf100_030.o" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
WANT="OTHERQ RD WR allocb autocon bcmp bcopy bufcall bzero canput drv_priv flushq freemsg getmajor getminor getq ifstats makedevice memcpy panicstr putbq putnext putq qenable qreply sptalloc unbufcall "
[ "$U" = "$WANT" ] || { echo "[FAIL] xsurf100_030.o imports: $U"; exit 1; }
echo "[OK] imports only what the stock kernel has"
D=$($NM "$B/xsurf100_030.o" | awk '$2 ~ /^[TDB]$/ {print $3}' | LC_ALL=C sort | tr '\n' ' ')
[ "$D" = "xsurf100info xsurf100init xsurf100intr " ] || { echo "[FAIL] xsurf100_030.o defines: $D"; exit 1; }
echo "[OK] global symbols: $D"
kit_pad "$B/xsurf100_030.o" "$B/xsurf100_pad.o"

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/xsurf100_030.o" "$B/xsurf100_pad.o" "$HERE/src/patch_xsurf100_cdevsw.py" \
	"$KIT_INT2" "$KIT_IOINIT" "$KIT_RELOCS" "$OUT/"
kit_info "$OUT" id=xsurf100 version=$VERSION major=56 node=/dev/xsf0 line=030 init=xsurf100init \
	intr=xsurf100intr "entries=xsurf100info xsurf100init xsurf100intr" streams=yes
kit_sums "$OUT" xsurf100_030.o xsurf100_pad.o patch_xsurf100_cdevsw.py patch_int2.py \
	patch_io_init.py check_relink_relocs.py KIT.info
rm -rf "$B"
echo "[OK] xsurf100 kit $VERSION in $OUT"
cat "$OUT/KIT.info" "$OUT/KIT.sha256"
