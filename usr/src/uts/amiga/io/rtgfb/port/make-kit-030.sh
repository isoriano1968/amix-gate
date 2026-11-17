#!/bin/sh
# make-kit-030.sh [OUTDIR] -- the AMIX Installer's rtgfb kit (0.2) for STOCK
# 68030 kernels.  OUTDIR defaults to kit/ beside this script (gitignored).
#
# The AMIX Installer links /dev/rtgfb into the user's own /stand/unix on the
# host (amix-installer kernel.py), because that kernel cannot be distributed.
# It needs only our own pieces, which this script builds and checks once:
#
#   rtgfb_030.o             src/rtgfb.c for the stock 2 KB-page 68030 kernel
#   rtgfb_pad.o             pads the kernel's .data back to a multiple of 4
#   patch_rtgfb_cdevsw.py   retargets cdevsw[54] (by symbol, asserting nodev)
#   patch_io_init.py        io_init[0] parinit -> the installer's driver chain
#                           amixdrv_init (port/src; the same file in every kit)
#   check_relink_relocs.py  the relocation check every relink runs
#   KIT.info                id, major, node, line, init, entries, streams
#   KIT.sha256              checksums of all of these, in link/run order
#
# rtgfbinit only searches for the board and names it (silent without one);
# it no longer calls parinit: amixdrv_init does (jsr parinit, jsr rtgfbinit).
# Kernel code is compiled with the gcc 2.7.2.3 cross compiler only.
#
# Two ways to run it:
#  - from an amix-040-060-port checkout that has the rtgfb files (install.sh
#    copies them): its tools/config-load.sh gives AMIX_KERNEL_CFLAGS;
#  - standalone from amix-gate (the package build): set AMIX_ROOT to an AMIX
#    2.1c tree with usr/include and usr/sys (github.com/amigaux/amix-system),
#    and have m68k-cbm-sysv4-gcc (developer/amix/gcc-27) on PATH.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
if [ -f "$HERE/tools/kit-lib.sh" ]; then PORT=$HERE; else PORT=$(cd "$HERE/../../../port" && pwd); fi
. "$PORT/tools/kit-lib.sh"
kit_env
OUT=${1:-$HERE/kit}
VERSION=0.2

B="$HERE/build/rtgfbkit"; rm -rf "$B"; mkdir -p "$B"
cp "$HERE/src/rtgfb.c" "$HERE/src/rtgfb.h" "$B/"
CF="$AMIX_KERNEL_CFLAGS -Dsigned=__signed__"
(cd "$B" && m68k-cbm-sysv4-gcc $CF -I. -c rtgfb.c -o rtgfb_030.o)
kit_check_030 "$B/rtgfb_030.o"
$NM "$B/rtgfb_030.o" | grep -q " U parinit$" && { echo "[FAIL] rtgfb_030.o still calls parinit"; exit 1; }
kit_pad "$B/rtgfb_030.o" "$B/rtgfb_pad.o"

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/rtgfb_030.o" "$B/rtgfb_pad.o" "$OUT/"
cp "$HERE/src/patch_rtgfb_cdevsw.py" "$KIT_IOINIT" "$KIT_RELOCS" "$OUT/"
kit_info "$OUT" id=rtgfb version=$VERSION major=54 node=/dev/rtgfb line=030 init=rtgfbinit \
	"entries=rtgfbinit rtgfbopen rtgfbclose rtgfbioctl rtgfbmmap" streams=no
kit_sums "$OUT" rtgfb_030.o rtgfb_pad.o patch_rtgfb_cdevsw.py patch_io_init.py \
	check_relink_relocs.py KIT.info
rm -rf "$B"
echo "[OK] rtgfb kit $VERSION in $OUT"
cat "$OUT/KIT.info" "$OUT/KIT.sha256"
