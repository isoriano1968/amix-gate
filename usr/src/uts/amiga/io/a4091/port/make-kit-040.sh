#!/bin/sh
# make-kit-040.sh [OUTDIR] -- the AMIX Installer's a4091 kit for the 68040/
# 68060 port kernels: the NCR 53C710 Zorro III SCSI boards -- Commodore A4091
# (AutoConfig 0x0202/0x54), A4092 and A4770 (SCSI.ME, 0xc0de/0x01, 0x02) --
# as controllers of sd.c, so their drives are the stock dd disks, block
# major 18 / char major 40, /dev/dsk/cNdUsS and /dev/rdsk/cNdUsS (unit = SCSI
# ID).  OUTDIR defaults to kit-040/ beside this script (build/, kit*/: gitignored).
#
#   a4091_040.o             src/a4091_kit.c: ../../src/a4091-wr.c (amigaux,
#                           Jussi Alanaerae, MIT; ../../UPSTREAM) as its static
#                           040/060 arm (-DA4091_KVA -DA4091_D42), plus the
#                           sdcardbase() it needs and the queue functions
#                           a4091q, a4092q, a4770q
#   a4091_kvmap.o           the port's src/devkvmap040.s, its two functions
#                           named a4091_kvmap/a4091_kvunmap (private copy, as
#                           the z3660scsi and va2000 kits carry theirs)
#   a4091_card.o            src/a4091_card.s: the six-row scsicard[]
#   a4091_pad.o             pads the kernel's .data back to a multiple of 4
#   patch_a4091_card.py     init()'s `lea scsicard` -> a4091_scsicard, loop
#                           bound 2 -> 5 (located by symbol, asserted)
#   patch_io_init.py        the installer's io_init chain (no init of its own)
#   check_relink_relocs.py  the loader's relocation check
#   LICENSE.a4091           the driver's MIT licence
#   KIT.info, KIT.sha256
#
# Polled, no init: the driver maps a board on its first request, its two
# pages noncacheable (dev_kvmap NCS).  Every transfer goes through the port
# kernel's D42 service dma_prepare/dma_complete (port/src/dma_cache040.s:
# cpushl/cinvl over the buffer's 16-byte lines), the arm upstream ran with
# root on a real A4092 in an A4000D with a Z3660.
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
OUT=${1:-$HERE/kit-040}
VERSION=0.1

B="$HERE/build/a4091kit040"; rm -rf "$B"; mkdir -p "$B"
. "$HERE/pins.sh"
cp "$SRC/a4091-wr.c" "$SRC/a4091_dmarec.h" "$HERE/src/a4091_kit.c" "$B/"
ALIEN=
for d in "${AMIX_ROOT:-/nonexistent}/usr/sys/amiga/alien"; do
	[ -f "$d/rico.h" ] && [ -f "$d/sd.h" ] && { ALIEN=$d; break; }
done
[ -n "$ALIEN" ] || { echo "[FAIL] no rico.h/sd.h under \$AMIX_ROOT/usr/sys/amiga/alien"; exit 1; }
kit_modelb_sysroot "$B"
(cd "$B" && AMIX_SYSROOT="$KIT_SYSROOT" m68k-cbm-sysv4-gcc $KIT_CF040 -DA4091_KVA -DA4091_D42 \
	-Ddev_kvmap=a4091_kvmap -Ddev_kvunmap=a4091_kvunmap -Dsdcardbase=a4091_sdcardbase \
	-Dsigned=__signed__ -fno-common -I"$ALIEN" -I. -c a4091_kit.c -o a4091_040.o)
kit_check_040 "$B/a4091_040.o"
a4091_gates "$B/a4091_040.o" "a4091_kvmap a4091_kvunmap autocon bprobe dma_complete dma_prepare printf "
if $OBJDUMP -dr "$B/a4091_040.o" | grep -qE 'R_68K_32[[:space:]]+sptalloc|#1073741824'; then
	echo "[FAIL] the 040 arm references sptalloc or a fixed 0x40000000"; exit 1
fi
LC_ALL=C $OBJDUMP -d "$B/a4091_040.o" > "$B/a4091_040.lst"
python3 "$PORT/test-tools/scan060.py" "$B/a4091_040.lst" > "$B/scan060.txt"
grep -q 'CLEAN -- no candidate found' "$B/scan060.txt" || { cat "$B/scan060.txt"; echo "[FAIL] scan060"; exit 1; }
echo "[OK] scan060: no 68060-unimplemented instruction"
m68k-cbm-sysv4-gcc -m68040 -c "$PORT/src/devkvmap040.s" -o "$B/devkvmap040.o"
$OBJCOPY --redefine-sym dev_kvmap=a4091_kvmap --redefine-sym dev_kvunmap=a4091_kvunmap \
	"$B/devkvmap040.o" "$B/a4091_kvmap.o"
D=$($NM "$B/a4091_kvmap.o" | awk '$2=="T"{print $3}' | LC_ALL=C sort | tr '\n' ' ')
[ "$D" = "a4091_kvmap a4091_kvunmap " ] || { echo "[FAIL] a4091_kvmap.o defines: $D"; exit 1; }
m68k-cbm-sysv4-as -o "$B/a4091_card.o" "$HERE/src/a4091_card.s"
a4091_card_check "$B/a4091_card.o"
kit_pad "$B/a4091_040.o" "$B/a4091_kvmap.o" "$B/a4091_card.o" "$B/a4091_pad.o"

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/a4091_040.o" "$B/a4091_kvmap.o" "$B/a4091_card.o" "$B/a4091_pad.o" "$HERE/src/patch_a4091_card.py" \
	"$KIT_IOINIT" "$KIT_RELOCS" "$OUT/"
cp "$HERE/../LICENSE" "$OUT/LICENSE.a4091"
# major=: none of its own; bmajor/cmajor: the stock dd driver's
kit_info "$OUT" id=a4091 version=$VERSION major= bmajor=18 cmajor=40 \
	"node=/dev/dsk/cNdUsS /dev/rdsk/cNdUsS" line=040 init= intr= \
	"entries=a4091q a4092q a4770q a4091queue a4091_scsicard a4091_kvmap a4091_kvunmap" streams=no \
	"conflicts=z3660_scsicard buddha_scsicard"
kit_sums "$OUT" a4091_040.o a4091_kvmap.o a4091_card.o a4091_pad.o patch_a4091_card.py patch_io_init.py \
	check_relink_relocs.py LICENSE.a4091 KIT.info
rm -rf "$B"
echo "[OK] a4091 kit $VERSION (040) in $OUT"
cat "$OUT/KIT.info" "$OUT/KIT.sha256"
