#!/bin/sh
# make-kit-040.sh [OUTDIR] -- the AMIX Installer's hydra kit for the
# 68040/68060 PORT kernels (Model B, 4 KB pages): the Hydra Systems AmigaNet
# (DP8390/NE2000, Zorro II) as the STREAMS DLPI driver hydrainfo, /dev/hya0
# (major 47).  OUTDIR defaults to kit-040/ beside this script (gitignored).
#
#   hydra_040.o             ../hydra.c with the shared DP8390 core, compiled
#                           -m68040 against the Model-B headers.  The board is
#                           Zorro II only (dp_be_addr_ok), inside the port
#                           kernel's identity-mapped low 1 GB (DTT0,
#                           noncacheable), as the X-Surf 100 in Zorro II on the
#                           same kernel: no dev_kvmap, no kernel VA taken
#   hydra_pad.o             pads the kernel's .data back to a multiple of 4
#   patch_hydra_cdevsw.py, patch_int2.py, patch_io_init.py,
#   check_relink_relocs.py, KIT.info (line=040), KIT.sha256: as the 030 kit
#
# The version follows the 030 kit (make-kit-030.sh): the same driver source.
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
VERSION=0.4

B="$HERE/build/hydrakit040"; rm -rf "$B"; mkdir -p "$B/src"
cp "$SRC/hydra.c" "$SRC/hydrauser.h" "$CORE/dp8390.c" "$CORE/dp8390.h" "$CORE/dp8390reg.h" "$B/src/"
kit_modelb_sysroot "$B"
(cd "$B/src" && AMIX_SYSROOT="$KIT_SYSROOT" m68k-cbm-sysv4-gcc $KIT_CF040 -Dsigned=__signed__ \
	-I. -c hydra.c -o "$B/hydra_040.o")
kit_check_040 "$B/hydra_040.o"
U=$($NM "$B/hydra_040.o" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
WANT="OTHERQ RD WR allocb autocon bcmp bcopy bufcall bzero canput drv_priv flushq freemsg getmajor getminor getq ifstats makedevice memcpy panicstr putbq putnext putq qenable qreply unbufcall "
[ "$U" = "$WANT" ] || { echo "[FAIL] hydra_040.o imports: $U"; exit 1; }
echo "[OK] imports: $U"
kit_pad "$B/hydra_040.o" "$B/hydra_pad.o"

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/hydra_040.o" "$B/hydra_pad.o" "$HERE/src/patch_hydra_cdevsw.py" \
	"$KIT_INT2" "$KIT_IOINIT" "$KIT_RELOCS" "$OUT/"
kit_info "$OUT" id=hydra version=$VERSION major=47 node=/dev/hya0 line=040 init=hydrainit \
	intr=hydraintr "entries=hydrainfo hydrainit hydraintr" streams=yes
kit_sums "$OUT" hydra_040.o hydra_pad.o patch_hydra_cdevsw.py patch_int2.py \
	patch_io_init.py check_relink_relocs.py KIT.info
rm -rf "$B"
echo "[OK] hydra kit $VERSION (040) in $OUT"
cat "$OUT/KIT.info" "$OUT/KIT.sha256"
