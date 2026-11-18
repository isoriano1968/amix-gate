#!/bin/sh
# make-kit-030.sh [OUTDIR] -- the AMIX Installer's a4091 kit for STOCK 68030
# kernels: the NCR 53C710 Zorro III SCSI boards -- Commodore A4091
# (AutoConfig 0x0202/0x54), A4092 and A4770 (SCSI.ME, 0xc0de/0x01, 0x02) --
# as controllers of sd.c, so their drives are the stock dd disks, block
# major 18 / char major 40, /dev/dsk/cNdUsS and /dev/rdsk/cNdUsS (unit = SCSI
# ID).  OUTDIR defaults to kit/ beside this script (build/, kit*/: gitignored).
#
#   a4091_030.o             src/a4091_kit.c: ../../src/a4091-wr.c (amigaux,
#                           Jussi Alanaerae, MIT; ../../UPSTREAM) as its static
#                           030 driver, plus the sdcardbase() it needs and the
#                           queue functions a4091q, a4092q, a4770q
#   a4091_card.o            src/a4091_card.s: the six-row scsicard[]
#   a4091_pad.o             pads the kernel's .data back to a multiple of 4
#   patch_a4091_card.py     init()'s `lea scsicard` -> a4091_scsicard, loop
#                           bound 2 -> 5 (located by symbol, asserted)
#   patch_io_init.py        the installer's io_init chain (no init of its own)
#   check_relink_relocs.py  the loader's relocation check
#   LICENSE.a4091           the driver's MIT licence
#   KIT.info, KIT.sha256
#
# Polled, no init: the driver maps a board on its first request.  The 030
# driver does no cache maintenance (as the stock a2091.c); upstream ran it in
# Amiberry only.
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

B="$HERE/build/a4091kit030"; rm -rf "$B"; mkdir -p "$B"
. "$HERE/pins.sh"
cp "$SRC/a4091-wr.c" "$SRC/a4091_dmarec.h" "$HERE/src/a4091_kit.c" "$B/"
ALIEN=
for d in "${AMIX_ROOT:-/nonexistent}/usr/sys/amiga/alien"; do
	[ -f "$d/rico.h" ] && [ -f "$d/sd.h" ] && { ALIEN=$d; break; }
done
[ -n "$ALIEN" ] || { echo "[FAIL] no rico.h/sd.h under \$AMIX_ROOT/usr/sys/amiga/alien"; exit 1; }
(cd "$B" && m68k-cbm-sysv4-gcc $AMIX_KERNEL_CFLAGS -Dsdcardbase=a4091_sdcardbase \
	-Dsigned=__signed__ -fno-common -I"$ALIEN" -I. -c a4091_kit.c -o a4091_030.o)
kit_check_030 "$B/a4091_030.o"
a4091_gates "$B/a4091_030.o" "autocon printf sptalloc "
m68k-cbm-sysv4-as -o "$B/a4091_card.o" "$HERE/src/a4091_card.s"
a4091_card_check "$B/a4091_card.o"
kit_pad "$B/a4091_030.o" "$B/a4091_card.o" "$B/a4091_pad.o"

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/a4091_030.o" "$B/a4091_card.o" "$B/a4091_pad.o" "$HERE/src/patch_a4091_card.py" \
	"$KIT_IOINIT" "$KIT_RELOCS" "$OUT/"
cp "$HERE/../LICENSE" "$OUT/LICENSE.a4091"
# major=: none of its own; bmajor/cmajor: the stock dd driver's
kit_info "$OUT" id=a4091 version=$VERSION major= bmajor=18 cmajor=40 \
	"node=/dev/dsk/cNdUsS /dev/rdsk/cNdUsS" line=030 init= intr= \
	"entries=a4091q a4092q a4770q a4091queue a4091_scsicard" streams=no \
	"conflicts=z3660_scsicard buddha_scsicard"
kit_sums "$OUT" a4091_030.o a4091_card.o a4091_pad.o patch_a4091_card.py patch_io_init.py \
	check_relink_relocs.py LICENSE.a4091 KIT.info
rm -rf "$B"
echo "[OK] a4091 kit $VERSION (030) in $OUT"
cat "$OUT/KIT.info" "$OUT/KIT.sha256"
