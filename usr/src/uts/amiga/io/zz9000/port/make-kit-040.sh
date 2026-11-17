#!/bin/sh
# make-kit-040.sh [OUTDIR] -- the AMIX Installer's zz9000 kit for the
# 68040/68060 PORT kernels (Model B, 4 KB pages): /dev/zz9000 (major 49) for
# the MNT ZZ9000 on Zorro II in an A3000/A4000 with a 68040/68060 card, so a
# generic-variant kernel (tools/amix-kernel-040) can have it.  OUTDIR defaults
# to kit-040/ beside this script (gitignored).
#
#   zz9000_040.o            overlay/usr/sys/amiga/driver/zz9000/zz9000.c
#                           compiled -m68040 against the Model-B headers with
#                            -DZZ9K_FB_NC: the framebuffer registered
#                             noncacheable-not-serialised (hat_cm_fb_add), so
#                             stores drain through the 68060 store buffer
#                             (as rtgfb-040 and the z3660 variant do);
#                            -DZZ9K_NO_BUILTINFONT: a kit carries no
#                             Commodore console font (the 5x7 glyphs draw the
#                             optional RTG console, as in the 030 kit)
#                           No ZZ9K_TARGET_*: the ZZ9000 rows only (Zorro II
#                           product 3; product 4 named and refused).  The
#                           Z3660's RTG has its own driver and kit
#                           (io/z3660rtg, /dev/z3660rtg, major 55).  No
#                           ZZ9K_KVMAP (a Zorro II board is in the identity
#                           map), no ZZ9K_MONSW, no ZZ9K_MAP_BOARD.
#   zz9000_pad.o            pads the kernel's .data back to a multiple of 4
#   patch_zz9000_cdevsw.py  cdevsw[49] d_open..d_mmap (asserting nodev)
#   patch_io_init.py        io_init[0] parinit -> amixdrv_init (port/src)
#   check_relink_relocs.py  the loader's relocation check
#   KIT.info (line=040), KIT.sha256
#
# The z3660 kernel variant has this driver built in (with the Z3660 rows):
# kit-relink.sh refuses the kit there (the base already has zz9000init).
# zz9000init probes once at boot and is silent without a board.
# Kernel code is compiled with the gcc 2.7.2.3 cross compiler only.  Needs
# AMIX_ROOT (with usr/sys/amiga/console) and m68k-cbm-sysv4-gcc on PATH, as
# make-kit-030.sh.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../../../port" && pwd)
SRC=$(cd "$HERE/../overlay/usr/sys/amiga/driver/zz9000" && pwd)
. "$PORT/tools/kit-lib.sh"
kit_env
OUT=${1:-$HERE/kit-040}
VERSION=0.2
[ -f "$AMIX_ROOT/usr/sys/amiga/console/screen.h" ] || {
	echo "AMIX_ROOT has no usr/sys/amiga/console/screen.h" >&2; exit 1; }
# the RTG caps block in zz9000.h is io/rtgcaps.h's, byte for byte
sed -n '/^\/\* RTGCAPS BEGIN \*\/$/,/^\/\* RTGCAPS END \*\/$/p' "$HERE/../../rtgcaps.h" > "$HERE/.caps.a.$$"
sed -n '/^\/\* RTGCAPS BEGIN \*\/$/,/^\/\* RTGCAPS END \*\/$/p' "$SRC/zz9000.h" > "$HERE/.caps.b.$$"
if ! cmp -s "$HERE/.caps.a.$$" "$HERE/.caps.b.$$"; then
	rm -f "$HERE/.caps.a.$$" "$HERE/.caps.b.$$"
	echo "[FAIL] zz9000.h's RTGCAPS block differs from io/rtgcaps.h"; exit 1; fi
rm -f "$HERE/.caps.a.$$" "$HERE/.caps.b.$$"

B="$HERE/build/zz9000kit040"; rm -rf "$B"; mkdir -p "$B/amiga/driver/zz9000"
kit_modelb_sysroot "$B"
# zz9000.c includes ../../console/{screen,console}.h: the AMIX tree's layout
ln -s "$AMIX_ROOT/usr/sys/amiga/console" "$B/amiga/console"
cp "$SRC/zz9000.c" "$SRC/zz9000.h" "$B/amiga/driver/zz9000/"
(cd "$B/amiga/driver/zz9000" && AMIX_SYSROOT="$KIT_SYSROOT" m68k-cbm-sysv4-gcc $KIT_CF040 \
	-Dsigned=__signed__ -DZZ9K_NO_BUILTINFONT -DZZ9K_FB_NC -I. -c zz9000.c -o "$B/zz9000_040.o")
kit_check_040 "$B/zz9000_040.o"
U=$($NM "$B/zz9000_040.o" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
[ "$U" = "autocon bootinfo conputc copyin copyout hat_cm_fb_add printf uiomove " ] || {
	echo "[FAIL] zz9000_040.o imports: $U"; exit 1; }
echo "[OK] imports: $U(-DZZ9K_FB_NC took, no dev_kvmap)"
G=$($NM "$B/zz9000_040.o" | awk 'NF==3 && $2 ~ /^[A-Z]$/ && $2 != "U" {print $3}' | LC_ALL=C sort | tr '\n' ' ')
echo "[*] globals: $G"
kit_pad "$B/zz9000_040.o" "$B/zz9000_pad.o"

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/zz9000_040.o" "$B/zz9000_pad.o" "$HERE/src/patch_zz9000_cdevsw.py" \
	"$KIT_IOINIT" "$KIT_RELOCS" "$OUT/"
kit_info "$OUT" id=zz9000 version=$VERSION major=49 node=/dev/zz9000 line=040 init=zz9000init \
	"entries=zz9000init zz9000open zz9000close zz9000read zz9000write zz9000ioctl zz9000mmap" \
	streams=no
kit_sums "$OUT" zz9000_040.o zz9000_pad.o patch_zz9000_cdevsw.py patch_io_init.py \
	check_relink_relocs.py KIT.info
rm -rf "$B"
echo "[OK] zz9000 kit $VERSION (040) in $OUT"
cat "$OUT/KIT.info" "$OUT/KIT.sha256"
