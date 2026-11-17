#!/bin/sh
# make-kit-040.sh [OUTDIR] -- the AMIX Installer's z3660scsi kit for the
# 68040/68060 PORT kernels (Model B, 4 KB pages): the Z3660's SD card storage
# (the piscsi mailbox, AutoConfig 0x144B0001) as a controller of sd.c, so its
# drives are the stock dd disks, block major 18 / char major 40,
# /dev/dsk/cNdUsS and /dev/rdsk/cNdUsS.  The Z3660 is a 68060 accelerator, so
# there is no 68030 kit.  OUTDIR defaults to kit-040/ beside this script
# (gitignored).
#
#   z3660scsi_040.o         ../src/z3660.c (Jussi Alanärä's z3660scsi, with
#                           the dev_kvmap mapping and DI-1 of this tree) with
#                           patches/ applied in order -- 0001 is ZS-05, the
#                           driver of the A3000 + Z3660 kernel (its code is
#                           compared below with the object that kernel links)
#                           -- compiled -m68040 against the Model-B headers
#                           with -DZ3660_KVMAP, and dev_kvmap/dev_kvunmap
#                           named z3660scsi_kvmap/z3660scsi_kvunmap
#   z3660scsi_kvmap.o       port/src/devkvmap040.s with the same two names
#   z3660scsi_card.o        src/z3660scsi_card040.s: the four-row scsicard[]
#                           (the stock three, then the Z3660)
#   z3660scsi_pad.o         pads the kernel's .data back to a multiple of 4
#   patch_z3660scsi_card.py init()'s `lea scsicard` -> z3660_scsicard, loop
#                           bound 2 -> 3 (located by symbol, asserted)
#   patch_io_init.py        (unused by this kit: no init; for the chain of the
#                           kits linked with it, as in every kit)
#   check_relink_relocs.py  the loader's relocation check
#   LICENSE.z3660scsi       the driver's MIT licence
#   KIT.info (line=040), KIT.sha256
#
# No device switch slot, no init, no interrupt: the piscsi mailbox is
# synchronous (the command-register write is the whole transfer), and sd.c
# registers the board at the first open of a dd disk, when its init() walks
# scsicard[] with autocon().  Without a Z3660 in the AutoConfig list the
# driver is never called and touches nothing; the fixed-window probe (AGA
# only) is reached through z3660present(), which only the module form and
# kerntools' generated sd.c call -- nothing in this kit.  So no patch is
# needed for a silent machine without the board.
#
# Not in the kit: the root device.  rootdev and the swap name are where the
# installer puts root (card = the Z3660's place among the SCSI controllers
# found, sorted by board address; unit = the SD image's piscsi unit), so the
# installer stamps them itself (tools/stamp-rootcard.py of the port line:
# --card 1 --unit 0 on an A3000 is /dev/dsk/c8d0s1).
#
# dev_kvmap: a Z3660 at or above VSECT1 (autoconfig_rtg YES, 0x50000000 on an
# A3000) is mapped noncacheable through the port's dev_kvmap; privately named
# for the reason in io/z3660net/port/make-kit-040.sh (the va2000, z3660eth and
# zz9000eth kits and kernels like m6-msp carry their own copies).
#
# Kernel code is compiled with the gcc 2.7.2.3 cross compiler only.  Needs
# AMIX_ROOT (or a port config.sh), with usr/sys/amiga/alien (rico.h, sd.h),
# and m68k-cbm-sysv4-gcc on PATH, as the other kits.  REF_OBJ=<z3660_040.o of
# the port line's build> also compares the object with the box kernel's.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../../../port" && pwd)
SRC=$(cd "$HERE/../src" && pwd)
. "$PORT/tools/kit-lib.sh"
kit_env
OUT=${1:-$HERE/kit-040}
VERSION=0.1

B="$HERE/build/z3660scsikit040"; rm -rf "$B"; mkdir -p "$B/src"
cp "$SRC/z3660.c" "$B/src/"
for p in "$HERE"/patches/*.patch; do
	(cd "$B" && patch -p1 -s --no-backup-if-mismatch < "$p") || {
		echo "[FAIL] $(basename "$p") does not apply"; exit 1; }
	echo "[OK] $(basename "$p")"
done
kit_modelb_sysroot "$B"
# z3660.c includes the alien SCSI headers by bare name ("rico.h", "sd.h")
ALIEN=
for d in "${AMIX_ROOT:-/nonexistent}/usr/sys/amiga/alien" "$KIT_SYSROOT/usr/sys/amiga/alien"; do
	[ -f "$d/rico.h" ] && [ -f "$d/sd.h" ] && { ALIEN=$d; break; }
done
[ -n "$ALIEN" ] || { echo "[FAIL] no rico.h/sd.h under \$AMIX_ROOT/usr/sys/amiga/alien"; exit 1; }
(cd "$B/src" && AMIX_SYSROOT="$KIT_SYSROOT" m68k-cbm-sysv4-gcc $KIT_CF040 -DZ3660_KVMAP \
	-Ddev_kvmap=z3660scsi_kvmap -Ddev_kvunmap=z3660scsi_kvunmap -I"$ALIEN" -I. \
	-c z3660.c -o "$B/z3660scsi_040.o")
kit_check_040 "$B/z3660scsi_040.o"
U=$($NM "$B/z3660scsi_040.o" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
WANT="autocon bcopy bootinfo bprobe cputype printf z3660scsi_kvmap "
[ "$U" = "$WANT" ] || { echo "[FAIL] z3660scsi_040.o imports: $U"; exit 1; }
echo "[OK] imports: $U(-DZ3660_KVMAP took)"
if [ -n "${REF_OBJ:-}" ]; then
	# the box kernel's object (dev_kvmap by its own name): the same code, data
	# and relocations, symbol for symbol
	cmpdump() { $OBJDUMP -s -j .text -j .data "$1" | tail -n +3 > "$2.s"
		$OBJDUMP -r "$1" | tail -n +3 | sed 's/z3660scsi_kvmap/dev_kvmap/' > "$2.r"
		$NM "$1" | sed 's/z3660scsi_kvmap/dev_kvmap/' | LC_ALL=C sort > "$2.n"; }
	cmpdump "$B/z3660scsi_040.o" "$B/kit"; cmpdump "$REF_OBJ" "$B/ref"
	for x in s r n; do cmp -s "$B/kit.$x" "$B/ref.$x" || {
		echo "[FAIL] z3660scsi_040.o differs from $REF_OBJ ($x)"; diff "$B/ref.$x" "$B/kit.$x" | head; exit 1; }; done
	echo "[OK] .text, .data, relocations and symbols byte for byte those of $REF_OBJ"
fi
m68k-cbm-sysv4-gcc -m68040 -c "$PORT/src/devkvmap040.s" -o "$B/devkvmap040.o"
$OBJCOPY --redefine-sym dev_kvmap=z3660scsi_kvmap --redefine-sym dev_kvunmap=z3660scsi_kvunmap \
	"$B/devkvmap040.o" "$B/z3660scsi_kvmap.o"
D=$($NM "$B/z3660scsi_kvmap.o" | awk '$2=="T"{print $3}' | LC_ALL=C sort | tr '\n' ' ')
[ "$D" = "z3660scsi_kvmap z3660scsi_kvunmap " ] || { echo "[FAIL] z3660scsi_kvmap.o defines: $D"; exit 1; }
m68k-cbm-sysv4-as -o "$B/z3660scsi_card.o" "$HERE/src/z3660scsi_card040.s"
D=$($NM "$B/z3660scsi_card.o" | awk '$2=="D"{print $3}' | tr '\n' ' ')
U=$($NM "$B/z3660scsi_card.o" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
[ "$D" = "z3660_scsicard " ] && [ "$U" = "a2090queue a2091queue a3091queue z3660queue " ] || {
	echo "[FAIL] z3660scsi_card.o defines $D, imports $U"; exit 1; }
kit_pad "$B/z3660scsi_040.o" "$B/z3660scsi_kvmap.o" "$B/z3660scsi_card.o" "$B/z3660scsi_pad.o"

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/z3660scsi_040.o" "$B/z3660scsi_kvmap.o" "$B/z3660scsi_card.o" "$B/z3660scsi_pad.o" \
	"$HERE/src/patch_z3660scsi_card.py" "$KIT_IOINIT" "$KIT_RELOCS" "$OUT/"
cp "$HERE/../LICENSE" "$OUT/LICENSE.z3660scsi"
# major=: none of its own; bmajor/cmajor: the stock dd driver's, which the
# kit uses and does not change
kit_info "$OUT" id=z3660scsi version=$VERSION major= bmajor=18 cmajor=40 \
	"node=/dev/dsk/cNdUsS /dev/rdsk/cNdUsS" line=040 init= intr= \
	"entries=z3660queue z3660_scsicard z3660scsi_kvmap z3660scsi_kvunmap" streams=no
kit_sums "$OUT" z3660scsi_040.o z3660scsi_kvmap.o z3660scsi_card.o z3660scsi_pad.o \
	patch_z3660scsi_card.py patch_io_init.py check_relink_relocs.py LICENSE.z3660scsi KIT.info
rm -rf "$B"
echo "[OK] z3660scsi kit $VERSION (040) in $OUT"
cat "$OUT/KIT.info" "$OUT/KIT.sha256"
