#!/bin/sh
# make-kit-030.sh [OUTDIR] -- the AMIX Installer's va2000 kit for STOCK 68030
# kernels: /dev/va2000 (major 68) for the MNT VA2000 on Zorro II.  OUTDIR
# defaults to kit/ beside this script (gitignored).
#
#   va2000_030.o            asokero's va2000.c (pinned, fetch-source.sh) with
#                           patches/, for the stock 2 KB-page kernel, without
#                           VA2000_KVA: the board is used through the Zorro II
#                           identity map, as the driver was first written
#   va2000_pad.o            pads the kernel's .data back to a multiple of 4
#   patch_va2000_cdevsw.py  cdevsw[68] d_open..d_mmap (by symbol, asserting nodev)
#   patch_io_init.py        io_init[0] parinit -> the installer's driver chain
#                           amixdrv_init, which calls va2000init (port/src)
#   check_relink_relocs.py  the loader's relocation check
#   KIT.info, KIT.sha256
#
# The stock 2 KB-page kernel is the driver's own first target (va2000-amix
# README: Zorro II, fw 1.9.0b2, 68030): va2000mmap's `>> 11` is right here.
# A Zorro III VA2000 needs the 68040/68060 port kernel (make-kit-040.sh);
# this kit names such a board and leaves it alone (patches/0001).  The
# /dev/screen type=2 changes of the driver's KERNEL_CHANGES.md (scrdev.c, c0.c,
# screen.c: Klaus Burkert's Xsvga) cannot be linked in: X reaches the card
# through /dev/va2000 (xzz9000's rtg layer), not through /dev/screen.
#
# Kernel code is compiled with the gcc 2.7.2.3 cross compiler only.  Needs
# AMIX_ROOT (an AMIX 2.1c tree with usr/include and usr/sys) and
# m68k-cbm-sysv4-gcc (developer/amix/gcc-27) on PATH.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../../../port" && pwd)
. "$PORT/tools/kit-lib.sh"
. "$HERE/fetch-source.sh"
kit_env
OUT=${1:-$HERE/kit}
VERSION=0.1

B="$HERE/build/va2000kit030"; rm -rf "$B"; mkdir -p "$B"
va2000_fetch "$B"
CF="$AMIX_KERNEL_CFLAGS -Dsigned=__signed__"
va2000_patch "$VA2000_TREE/src/va2000.c"
(cd "$VA2000_TREE/src" && m68k-cbm-sysv4-gcc $CF -I. -c va2000.c -o "$B/va2000_030.o")
kit_check_030 "$B/va2000_030.o"
U=$($NM "$B/va2000_030.o" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
[ "$U" = "autocon copyin copyout printf uiomove " ] || {
	echo "[FAIL] va2000_030.o imports: $U"; exit 1; }
echo "[OK] imports only what the stock kernel has: $U"
kit_pad "$B/va2000_030.o" "$B/va2000_pad.o"

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/va2000_030.o" "$B/va2000_pad.o" "$HERE/src/patch_va2000_cdevsw.py" \
	"$KIT_IOINIT" "$KIT_RELOCS" "$OUT/"
cp "$VA2000_TREE/src/LICENSE" "$OUT/LICENSE.va2000"
kit_info "$OUT" id=va2000 version=$VERSION major=68 node=/dev/va2000 line=030 init=va2000init \
	"entries=va2000init va2000open va2000close va2000read va2000write va2000ioctl va2000mmap" \
	streams=no
kit_sums "$OUT" va2000_030.o va2000_pad.o patch_va2000_cdevsw.py patch_io_init.py \
	check_relink_relocs.py LICENSE.va2000 KIT.info
rm -rf "$B"
echo "[OK] va2000 kit $VERSION (030) in $OUT"
cat "$OUT/KIT.info" "$OUT/KIT.sha256"
