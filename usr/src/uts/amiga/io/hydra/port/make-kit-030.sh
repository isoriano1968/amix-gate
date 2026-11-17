#!/bin/sh
# make-kit-030.sh [OUTDIR] -- the AMIX Installer's hydra kit for STOCK 68030
# kernels: the Hydra Systems AmigaNet (DP8390/NE2000, Zorro II) as the STREAMS
# DLPI driver hydrainfo, /dev/hya0 (major 47).  OUTDIR defaults to kit/
# beside this script (gitignored).
#
#   hydra_030.o             ../hydra.c for the stock 2 KB-page kernel
#   hydra_pad.o             pads the kernel's .data back to a multiple of 4
#   patch_hydra_cdevsw.py   cdevsw[47].d_str -> hydrainfo (STREAMS: the slot's
#                           d_xhalt relocation moved to d_str, by symbol)
#   patch_int2.py           int2_tbl's aenintr entry -> the installer's level-2
#                           chain amixdrv_int2, which calls aenintr, then
#                           hydraintr (KIT.info intr=; port/src)
#   patch_io_init.py        io_init[0] parinit -> the installer's driver chain
#                           amixdrv_init, which calls hydrainit (port/src)
#   check_relink_relocs.py  the loader's relocation check
#   KIT.info, KIT.sha256
#
# What the stock kernel needs for a STREAMS driver, and how it gets each:
#   - cdevsw[47].d_str = &hydrainfo (kernel.c: ND x10, notty, &hydrainfo,
#     nullflag): spec_open/stropen, clnopen, qattach and sad's apush find a
#     STREAMS driver by d_str alone; fmodsw is for pushable modules only, the
#     Hydra is a driver, not a module.  -> patch_hydra_cdevsw.py
#   - hydraintr in int2_tbl[] (kernel.c), which has no spare slot
#     -> the shared level-2 chain amixdrv_int2 + patch_int2.py (intr=)
#   - hydrainit in init_tbl[] (kernel.c); here io_init's chain amixdrv_init
#     runs it, a little later in main() and as harmlessly: it only searches
#     for the board (autocon, then the driver's slot probes) and says nothing.
#
# Kernel code is compiled with the gcc 2.7.2.3 cross compiler only.  Needs
# AMIX_ROOT (an AMIX 2.1c tree with usr/include and usr/sys) and
# m68k-cbm-sysv4-gcc (developer/amix/gcc-27) on PATH.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../../../port" && pwd)
SRC=$(cd "$HERE/.." && pwd)
. "$PORT/tools/kit-lib.sh"
kit_env
OUT=${1:-$HERE/kit}
VERSION=0.4

B="$HERE/build/hydrakit"; rm -rf "$B"; mkdir -p "$B"
cp "$SRC/hydra.c" "$SRC/hydrauser.h" "$SRC/../dp8390/dp8390.c" "$SRC/../dp8390/dp8390.h" \
	"$SRC/../dp8390/dp8390reg.h" "$B/"
CF="$AMIX_KERNEL_CFLAGS -Dsigned=__signed__"
(cd "$B" && m68k-cbm-sysv4-gcc $CF -I. -c hydra.c -o hydra_030.o)
kit_check_030 "$B/hydra_030.o"
U=$($NM "$B/hydra_030.o" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
WANT="OTHERQ RD WR allocb autocon bcmp bcopy bufcall bzero canput drv_priv flushq freemsg getmajor getminor getq ifstats makedevice memcpy panicstr putbq putnext putq qenable qreply unbufcall "
[ "$U" = "$WANT" ] || { echo "[FAIL] hydra_030.o imports: $U"; exit 1; }
echo "[OK] imports only what the stock kernel has"
kit_pad "$B/hydra_030.o" "$B/hydra_pad.o"

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/hydra_030.o" "$B/hydra_pad.o" "$HERE/src/patch_hydra_cdevsw.py" \
	"$KIT_INT2" "$KIT_IOINIT" "$KIT_RELOCS" "$OUT/"
kit_info "$OUT" id=hydra version=$VERSION major=47 node=/dev/hya0 line=030 init=hydrainit \
	intr=hydraintr "entries=hydrainfo hydrainit hydraintr" streams=yes
kit_sums "$OUT" hydra_030.o hydra_pad.o patch_hydra_cdevsw.py patch_int2.py \
	patch_io_init.py check_relink_relocs.py KIT.info
rm -rf "$B"
echo "[OK] hydra kit $VERSION in $OUT"
cat "$OUT/KIT.info" "$OUT/KIT.sha256"
