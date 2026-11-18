#!/bin/sh
# make-kit-030.sh [OUTDIR] -- the AMIX Installer's buddha kit for STOCK 68030
# kernels: the Individual Computers Buddha and Catweasel Z-II (Zorro II IDE,
# AutoConfig 0x1212/0x00 and 0x1212/0x2A) as controllers of sd.c, so their
# drives are the stock dd disks, block major 18 / char major 40,
# /dev/dsk/cNdUsS and /dev/rdsk/cNdUsS (ports 0, 1, 2: units 6/5, 3/2, 1/0).
# OUTDIR defaults to kit/ beside this script (build/, kit*/: gitignored).
#
#   buddha_030.o            src/buddha_kit.c: ../../src/ide.c (amix-gayle-ide,
#                           Emre Bastuz, MIT; ../../UPSTREAM) as its Buddha
#                           module form, with the module core's four calls
#                           made here; buddhainit, budqueue, cwqueue
#   buddha_card.o           src/buddha_card.s: the five-row scsicard[]
#   buddha_pad.o            pads the kernel's .data back to a multiple of 4
#   patch_buddha_card.py    init()'s `lea scsicard` -> buddha_scsicard, loop
#                           bound 2 -> 4 (located by symbol, asserted)
#   patch_io_init.py        io_init[0] parinit -> the installer's driver chain
#                           amixdrv_init, which calls buddhainit (port/src)
#   check_relink_relocs.py  the loader's relocation check
#   LICENSE.buddha          the driver's MIT licence
#   KIT.info, KIT.sha256
#
# Polled I/O: interrupts need the amigaux module core's deferred self-test,
# which these kernels do not have (src/buddha_kit.c), so no intr=.
#
# Kernel code is compiled with the gcc 2.7.2.3 cross compiler only.  Needs
# AMIX_ROOT (an AMIX 2.1c tree with usr/include and usr/sys, and
# usr/sys/amiga/alien: rico.h, sd.h) and m68k-cbm-sysv4-gcc on PATH.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../../../port" && pwd)
SRC=$(cd "$HERE/../src" && pwd)
. "$PORT/tools/kit-lib.sh"
kit_env
OUT=${1:-$HERE/kit}
VERSION=0.1

B="$HERE/build/buddhakit030"; rm -rf "$B"; mkdir -p "$B"
# the upstream files as published at ../../UPSTREAM's commit, unchanged
(cd "$SRC/.." && sha256sum -c --quiet - <<'SUMS') || { echo "[FAIL] src/ is not the pinned upstream"; exit 1; }
69f4ead6e1a3662b3697b54430ca8e23e9209c0f700afe26d943a9b39ebc8148  src/ide.c
987f917aa6f286fe5074ff00c7d9f313e587497443375b853d750330877c37e0  src/buddha.c
b2a9d95bb7959880868ac8f29fcb7268c02a9401597ba1d153b0055bdc5c9340  LICENSE
SUMS
cp "$SRC/ide.c" "$HERE/src/buddha_kit.c" "$B/"
ALIEN=
for d in "${AMIX_ROOT:-/nonexistent}/usr/sys/amiga/alien"; do
	[ -f "$d/rico.h" ] && [ -f "$d/sd.h" ] && { ALIEN=$d; break; }
done
[ -n "$ALIEN" ] || { echo "[FAIL] no rico.h/sd.h under \$AMIX_ROOT/usr/sys/amiga/alien"; exit 1; }
(cd "$B" && m68k-cbm-sysv4-gcc -I"$HERE/src/include" $AMIX_KERNEL_CFLAGS -Dsigned=__signed__ \
	-fno-common -I"$ALIEN" -I. -c buddha_kit.c -o buddha_030.o)
kit_check_030 "$B/buddha_030.o"
U=$($NM "$B/buddha_030.o" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
WANT="autocon bprobe printf sleep timeout wakeup "
[ "$U" = "$WANT" ] || { echo "[FAIL] buddha_030.o imports: $U"; exit 1; }
echo "[OK] imports only what the stock kernel has: $U"
for s in buddhainit budqueue cwqueue; do
	$NM "$B/buddha_030.o" | grep -q " T $s\$" || { echo "[FAIL] buddha_030.o does not define $s"; exit 1; }
done
m68k-cbm-sysv4-as -o "$B/buddha_card.o" "$HERE/src/buddha_card.s"
D=$($NM "$B/buddha_card.o" | awk '$2=="D"{print $3}' | tr '\n' ' ')
U=$($NM "$B/buddha_card.o" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
[ "$D" = "buddha_scsicard " ] && [ "$U" = "a2090queue a2091queue a3091queue budqueue cwqueue " ] || {
	echo "[FAIL] buddha_card.o defines $D, imports $U"; exit 1; }
kit_pad "$B/buddha_030.o" "$B/buddha_card.o" "$B/buddha_pad.o"

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/buddha_030.o" "$B/buddha_card.o" "$B/buddha_pad.o" "$HERE/src/patch_buddha_card.py" \
	"$KIT_IOINIT" "$KIT_RELOCS" "$OUT/"
cp "$HERE/../LICENSE" "$OUT/LICENSE.buddha"
# major=: none of its own; bmajor/cmajor: the stock dd driver's
kit_info "$OUT" id=buddha version=$VERSION major= bmajor=18 cmajor=40 \
	"node=/dev/dsk/cNdUsS /dev/rdsk/cNdUsS" line=030 init=buddhainit intr= \
	"entries=buddhainit budqueue cwqueue buddha_scsicard" streams=no conflicts=z3660_scsicard
kit_sums "$OUT" buddha_030.o buddha_card.o buddha_pad.o patch_buddha_card.py patch_io_init.py \
	check_relink_relocs.py LICENSE.buddha KIT.info
rm -rf "$B"
echo "[OK] buddha kit $VERSION (030) in $OUT"
cat "$OUT/KIT.info" "$OUT/KIT.sha256"
