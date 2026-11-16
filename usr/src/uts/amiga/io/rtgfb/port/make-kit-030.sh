#!/bin/sh
# make-kit-030.sh OUTDIR -- the installer's rtgfb kit for STOCK 68030 kernels.
#
# The AMIX Installer links /dev/rtgfb into the user's own /stand/unix on the
# host (amix-installer kernel.py), because that kernel cannot be distributed.
# It needs only our own pieces, which this script builds and checks once:
#
#   rtgfb_030.o             src/rtgfb.c for the stock 2 KB-page 68030 kernel
#   rtgfb_pad.o             pads the kernel's .data back to a multiple of 4
#   patch_rtgfb_cdevsw.py   retargets cdevsw[54] (by symbol, asserting nodev)
#   patch_rtgfb_init.py     retargets io_init[0] parinit -> rtgfbinit (by
#                           symbol, asserting parinit): the board named at boot
#   check_relink_relocs.py  the relocation check every relink runs
#   KIT.sha256              checksums of the five files
#
# The compile and the checks are the ones relink-030-rtgfb.sh runs; the kit is
# the same objects, built once.  Kernel code is compiled with the gcc 2.7.2.3
# cross compiler (m68k-cbm-sysv4-gcc) only.
#
# Two ways to run it:
#  - from an amix-040-060-port checkout that has the rtgfb files (install.sh
#    copies them): its tools/config-load.sh gives AMIX_KERNEL_CFLAGS;
#  - standalone from amix-gate (the package build): set AMIX_ROOT to an AMIX
#    2.1c tree with usr/include and usr/sys (github.com/amigaux/amix-system),
#    and have m68k-cbm-sysv4-gcc (developer/amix/gcc-27) on PATH.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
if [ -f "$HERE/tools/config-load.sh" ]; then
	. "$HERE/tools/config-load.sh"
else
	[ -d "${AMIX_ROOT:-}/usr/sys/amiga/inc" ] || {
		echo "AMIX_ROOT must name an AMIX 2.1c tree with usr/include and usr/sys" >&2; exit 1; }
	# the flags of gcc-cross-amix's env.sh, which the port's relinks use
	AMIX_KERNEL_CFLAGS="-O -traditional -fno-builtin -Dinline=__inline__ -Dvolatile=__volatile__ -Dconst=__const__ -m68020 -D_KERNEL -DSVR40 -DSVR4 -I$AMIX_ROOT/usr/include -I$AMIX_ROOT/usr/sys -I$AMIX_ROOT/usr/sys/amiga/inc"
fi
# check_relink_relocs.py: the port's src/, or amix-gate's uts/amiga/port/src
RELOCS="$HERE/src/check_relink_relocs.py"
[ -f "$RELOCS" ] || RELOCS="$HERE/../../../port/src/check_relink_relocs.py"
[ -f "$RELOCS" ] || { echo "check_relink_relocs.py not found" >&2; exit 1; }
OUT=$1
[ -n "$OUT" ] || { echo "usage: $0 OUTDIR"; exit 1; }
. "$HERE/tools/m68k-binutils.sh"

B="$HERE/build/rtgfbkit"; rm -rf "$B"; mkdir -p "$B" "$OUT"
cp "$HERE/src/rtgfb.c" "$HERE/src/rtgfb.h" "$B/"
CF="$AMIX_KERNEL_CFLAGS -Dsigned=__signed__"
(cd "$B" && m68k-cbm-sysv4-gcc $CF -I. -c rtgfb.c -o rtgfb_030.o)

# the page shift must be the stock one: by 11 (2 KB), never by 12
D=$($OBJDUMP -d "$B/rtgfb_030.o")
n11=$(echo "$D" | grep -A1 "moveq #11," | grep -cE "lsrl|asrl" || true)
n12=$(echo "$D" | grep -A1 "moveq #12," | grep -cE "lsrl|asrl" || true)
[ "$n12" -eq 0 ] && [ "$n11" -gt 0 ] || {
	echo "[FAIL] rtgfb_030.o page shifts: $n11 by 11, $n12 by 12 (want only 11)"; exit 1; }
# 68020/68030 code only
if echo "$D" | grep -E "\s(move16|cinv[alp]|cpush[alp]|pflushan?|pflushn)\s"; then
	echo "[FAIL] rtgfb_030.o has 68040/68060 instructions"; exit 1; fi

ODS=$(( 0x$($READELF -SW "$B/rtgfb_030.o" | awk '{gsub(/[][]/,"")} $2==".data"{print $6}') ))
printf '\t.data\n\t.space %d\n' $(( (4 - ODS % 4) % 4 )) > "$B/rtgfb_pad.s"
m68k-cbm-sysv4-gcc -c "$B/rtgfb_pad.s" -o "$B/rtgfb_pad.o"
$OBJCOPY --set-section-alignment .data=1 "$B/rtgfb_pad.o"

cp "$B/rtgfb_030.o" "$B/rtgfb_pad.o" "$OUT/"
cp "$HERE/src/patch_rtgfb_cdevsw.py" "$HERE/src/patch_rtgfb_init.py" "$RELOCS" "$OUT/"
(cd "$OUT" && sha256sum rtgfb_030.o rtgfb_pad.o patch_rtgfb_cdevsw.py \
	patch_rtgfb_init.py check_relink_relocs.py > KIT.sha256)
rm -rf "$B"
echo "[OK] rtgfb kit in $OUT"
cat "$OUT/KIT.sha256"
