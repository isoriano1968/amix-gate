#!/bin/sh
# make-kit-040.sh [OUTDIR] -- the AMIX Installer's xsurf100 kit for the
# 68040/68060 PORT kernels (Model B, 4 KB pages): the X-Surf 100 as the
# STREAMS DLPI driver xsurf100info, /dev/xsf0 (major 56).  OUTDIR defaults
# to kit-040/ beside this script (gitignored).
#
#   xsurf100_040.o          ../xsurf100.c with the shared DP8390 core,
#                           compiled -m68040 against the Model-B headers with
#                           -DXSURF100_KVMAP: a board above the identity-
#                           mapped 1 GB (Zorro III in an A3000/A4000) through
#                           dev_kvmap noncacheable serialised, named
#                           xsurf100_kvmap
#   xsurf100_kvmap.o        port/src/devkvmap040.s with dev_kvmap/dev_kvunmap
#                           renamed xsurf100_kvmap/xsurf100_kvunmap: the
#                           va2000 kit and kernels like m6-msp have their own
#                           dev_kvmap, so any combination links once
#   xsurf100_pad.o          pads the kernel's .data back to a multiple of 4
#   patch_xsurf100_cdevsw.py, patch_int2.py, patch_io_init.py,
#   check_relink_relocs.py, KIT.info (line=040), KIT.sha256: as the 030 kit
#
# Kernel code is compiled with the gcc 2.7.2.3 cross compiler only.  Needs
# AMIX_ROOT (or a port config.sh) and m68k-cbm-sysv4-gcc on PATH.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../../../port" && pwd)
SRC=$(cd "$HERE/.." && pwd)
CORE=$(cd "$HERE/../../dp8390" && pwd)
. "$PORT/tools/kit-lib.sh"
kit_env
OUT=${1:-$HERE/kit-040}
VERSION=0.2

B="$HERE/build/xsurf100kit040"; rm -rf "$B"; mkdir -p "$B/src"
cp "$SRC/xsurf100.c" "$SRC/xsurf100user.h" "$CORE/dp8390.c" "$CORE/dp8390.h" "$CORE/dp8390reg.h" "$B/src/"
kit_modelb_sysroot "$B"
(cd "$B/src" && AMIX_SYSROOT="$KIT_SYSROOT" m68k-cbm-sysv4-gcc $KIT_CF040 -Dsigned=__signed__ \
	-DXSURF100_KVMAP -I. -c xsurf100.c -o "$B/xsurf100_040.o")
kit_check_040 "$B/xsurf100_040.o"
U=$($NM "$B/xsurf100_040.o" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
WANT="OTHERQ RD WR allocb autocon bcmp bcopy bufcall bzero canput drv_priv flushq freemsg getmajor getminor getq ifstats makedevice memcpy panicstr putbq putnext putq qenable qreply unbufcall xsurf100_kvmap "
[ "$U" = "$WANT" ] || { echo "[FAIL] xsurf100_040.o imports: $U"; exit 1; }
echo "[OK] imports: $U(-DXSURF100_KVMAP took)"
m68k-cbm-sysv4-gcc -m68040 -c "$PORT/src/devkvmap040.s" -o "$B/devkvmap040.o"
$OBJCOPY --redefine-sym dev_kvmap=xsurf100_kvmap --redefine-sym dev_kvunmap=xsurf100_kvunmap \
	"$B/devkvmap040.o" "$B/xsurf100_kvmap.o"
D=$($NM "$B/xsurf100_kvmap.o" | awk '$2=="T"{print $3}' | LC_ALL=C sort | tr '\n' ' ')
[ "$D" = "xsurf100_kvmap xsurf100_kvunmap " ] || { echo "[FAIL] xsurf100_kvmap.o defines: $D"; exit 1; }
kit_pad "$B/xsurf100_040.o" "$B/xsurf100_kvmap.o" "$B/xsurf100_pad.o"

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/xsurf100_040.o" "$B/xsurf100_kvmap.o" "$B/xsurf100_pad.o" "$HERE/src/patch_xsurf100_cdevsw.py" \
	"$KIT_INT2" "$KIT_IOINIT" "$KIT_RELOCS" "$OUT/"
kit_info "$OUT" id=xsurf100 version=$VERSION major=56 node=/dev/xsf0 line=040 init=xsurf100init \
	intr=xsurf100intr "entries=xsurf100info xsurf100init xsurf100intr xsurf100_kvmap xsurf100_kvunmap" \
	streams=yes
kit_sums "$OUT" xsurf100_040.o xsurf100_kvmap.o xsurf100_pad.o patch_xsurf100_cdevsw.py patch_int2.py \
	patch_io_init.py check_relink_relocs.py KIT.info
rm -rf "$B"
echo "[OK] xsurf100 kit $VERSION (040) in $OUT"
cat "$OUT/KIT.info" "$OUT/KIT.sha256"
