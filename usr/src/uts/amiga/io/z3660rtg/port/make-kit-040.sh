#!/bin/sh
# make-kit-040.sh [OUTDIR] -- the AMIX Installer's z3660rtg kit for the
# 68040/68060 PORT kernels (Model B, 4 KB pages): /dev/z3660rtg (major 55),
# the Z3660 accelerator's RTG, for a generic-variant kernel
# (tools/amix-kernel-040).  OUTDIR defaults to kit-040/ beside this script
# (gitignored).
#
# Source: src/zz9000.c and src/zz9000.h are amix-z3660rtg 1.0 as published
# (github.com/jusii/amix-z3660rtg 91667de, MIT), pristine and checked against
# the pins below; port/patches/*.patch are applied in order:
#   0001-0007  the driver as it runs on the A3000 + Z3660 (dev_kvmap above
#              VSECT1, console, soft3D mapping, VT hooks, mode table, silent
#              without a board): io/zz9000's history
#   0008       colour modes per board row (the Z3660: no 32-bit) and
#              RTGIOC_GETCAPS: io/zz9000's change of 2026-11-17
#   0009       z3660rtg: the Z3660 rows only, its own names (z3660rtg*), the
#              fixed window bprobe()d and silent
# After 0008 the result must be io/zz9000's zz9000.c and zz9000.h byte for
# byte: the two drivers share every fix, and a change made to io/zz9000 alone
# stops this build until a patch here carries it.
#
#   z3660rtg_040.o          the patched source, -m68040 against the Model-B
#                           headers, with
#                            -DZZ9K_KVMAP: a board above VSECT1
#                             (autoconfig_rtg YES) mapped through dev_kvmap;
#                            -DZZ9K_FB_NC: the framebuffer registered
#                             noncacheable-not-serialised (hat_cm_fb_add);
#                            -DZZ9K_MAP_BOARD: mmap() reaches the whole
#                             board for soft3D (ZZ9KIOC_SOFT3D/BLITX);
#                            -DZZ9K_NO_BUILTINFONT: no Commodore console
#                             font in a kit (the 5x7 glyphs);
#                           the flags of the z3660 kernel variant's built-in
#                           driver, without its console font
#   devkvmap040.o           port/src/devkvmap040.s: dev_kvmap/dev_kvunmap,
#                           KIT.info provides=: left out when the base has
#                           them, linked once with va2000-040/rtgfb-040
#   z3660rtg_pad.o          pads the kernel's .data back to a multiple of 4
#   patch_z3660rtg_cdevsw.py cdevsw[55] d_open..d_mmap (asserting nodev)
#   patch_io_init.py        io_init[0] parinit -> amixdrv_init (port/src)
#   check_relink_relocs.py  the loader's relocation check
#   KIT.info (line=040, conflicts=zz9k_regoff_z3660), KIT.sha256
#
# conflicts=: the z3660 kernel variant drives the Z3660 RTG with its built-in
# zz9000 driver (major 49, kept for byte identity with the box); kit-relink.sh
# and the AMIX Installer refuse this kit on a base that has the symbol.
# z3660rtginit probes once at boot and is silent without the board.
# Kernel code is compiled with the gcc 2.7.2.3 cross compiler only.  Needs
# AMIX_ROOT (with usr/sys/amiga/console), m68k-cbm-sysv4-gcc on PATH and
# patch(1).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../../../port" && pwd)
TOP=$(cd "$HERE/.." && pwd)
ZZ=$(cd "$HERE/../../zz9000/overlay/usr/sys/amiga/driver/zz9000" && pwd)
. "$PORT/tools/kit-lib.sh"
kit_env
OUT=${1:-$HERE/kit-040}
VERSION=0.1
UPSTREAM_COMMIT=91667dec985e32cf652104c1f4028ced924e8d48
ZZ9000_C_SHA256=96a30b0a7438b130553527c7c760fa610979d03764d8ff0d29788007f24bf54d
ZZ9000_H_SHA256=e5d05a9c087ed7127abc581e151b11ce782cb882e4eb155577c50124c296d8ad
DRIFT_LAST=0008
[ -f "$AMIX_ROOT/usr/sys/amiga/console/screen.h" ] || {
	echo "AMIX_ROOT has no usr/sys/amiga/console/screen.h" >&2; exit 1; }
command -v patch >/dev/null || { echo "no patch(1) on PATH" >&2; exit 1; }

B="$HERE/build/z3660rtgkit040"; rm -rf "$B"; mkdir -p "$B/up/src" "$B/amiga/driver/z3660rtg"
cp "$TOP/src/zz9000.c" "$TOP/src/zz9000.h" "$B/up/src/"
(cd "$B/up/src" && printf '%s  zz9000.c\n%s  zz9000.h\n' "$ZZ9000_C_SHA256" "$ZZ9000_H_SHA256" |
	sha256sum -c --quiet) || { echo "[FAIL] src/ is not amix-z3660rtg $UPSTREAM_COMMIT"; exit 1; }
echo "[OK] src/: amix-z3660rtg ${UPSTREAM_COMMIT%${UPSTREAM_COMMIT#????????????}}, pristine"
for p in "$HERE"/patches/*.patch; do
	(cd "$B/up" && patch -p1 -s --no-backup-if-mismatch < "$p") || { echo "[FAIL] $(basename "$p") does not apply"; exit 1; }
	echo "[OK] $(basename "$p")"
	case $(basename "$p") in
	$DRIFT_LAST-*)
		for f in zz9000.c zz9000.h; do
			cmp -s "$B/up/src/$f" "$ZZ/$f" || {
				echo "[FAIL] patches up to $DRIFT_LAST do not give io/zz9000's $f: carry its change into a patch here"
				exit 1; }
		done
		echo "[OK] up to $DRIFT_LAST: io/zz9000's zz9000.c and zz9000.h, byte for byte" ;;
	esac
done
# the RTG caps block in zz9000.h is io/rtgcaps.h's, byte for byte
CAPS='/^\/\* RTGCAPS BEGIN \*\/$/,/^\/\* RTGCAPS END \*\/$/p'
sed -n "$CAPS" "$TOP/../rtgcaps.h" > "$B/caps.a"; sed -n "$CAPS" "$B/up/src/zz9000.h" > "$B/caps.b"
[ -s "$B/caps.a" ] && cmp -s "$B/caps.a" "$B/caps.b" || { echo "[FAIL] zz9000.h's RTGCAPS block is not io/rtgcaps.h's"; exit 1; }

kit_modelb_sysroot "$B"
# the driver includes ../../console/{screen,console}.h: the AMIX tree's layout
ln -s "$AMIX_ROOT/usr/sys/amiga/console" "$B/amiga/console"
cp "$B/up/src/zz9000.c" "$B/amiga/driver/z3660rtg/z3660rtg.c"
cp "$B/up/src/zz9000.h" "$B/amiga/driver/z3660rtg/"
(cd "$B/amiga/driver/z3660rtg" && AMIX_SYSROOT="$KIT_SYSROOT" m68k-cbm-sysv4-gcc $KIT_CF040 \
	-Dsigned=__signed__ -DZZ9K_KVMAP -DZZ9K_FB_NC -DZZ9K_MAP_BOARD -DZZ9K_NO_BUILTINFONT -I. \
	-c z3660rtg.c -o "$B/z3660rtg_040.o")
kit_check_040 "$B/z3660rtg_040.o"
U=$($NM "$B/z3660rtg_040.o" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
[ "$U" = "autocon bootinfo bprobe conputc copyin copyout dev_kvmap dev_kvunmap hat_cm_fb_add printf uiomove " ] || {
	echo "[FAIL] z3660rtg_040.o imports: $U"; exit 1; }
echo "[OK] imports: $U(-DZZ9K_KVMAP -DZZ9K_FB_NC took)"
G=$($NM "$B/z3660rtg_040.o" | awk 'NF==3 && $2 ~ /^[A-Z]$/ {print $3}' | LC_ALL=C sort | tr '\n' ' ')
for g in $G; do case $g in z3660rtg*) ;; *) echo "[FAIL] global $g is not z3660rtg's own"; exit 1 ;; esac; done
echo "[OK] globals all z3660rtg*: $G"
$NM "$B/z3660rtg_040.o" | grep -qE ' [dD] zz9k_regoff_z3660$' || { echo "[FAIL] no Z3660 register row"; exit 1; }
! $NM "$B/z3660rtg_040.o" | grep -qE ' [dD] zz9k_regoff_zz9000$' || { echo "[FAIL] the ZZ9000 rows are still in"; exit 1; }
strings "$B/z3660rtg_040.o" | grep -q '^Z3660 RTG$' || { echo "[FAIL] no Z3660 RTG row name"; exit 1; }
! strings "$B/z3660rtg_040.o" | grep -q 'zz9000:' || { echo "[FAIL] a console line still says zz9000:"; exit 1; }
echo "[OK] Z3660 rows only, console lines say z3660rtg:"
m68k-cbm-sysv4-gcc -m68040 -c "$PORT/src/devkvmap040.s" -o "$B/devkvmap040.o"
kit_pad "$B/z3660rtg_040.o" "$B/devkvmap040.o" "$B/z3660rtg_pad.o"

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/z3660rtg_040.o" "$B/devkvmap040.o" "$B/z3660rtg_pad.o" "$HERE/src/patch_z3660rtg_cdevsw.py" \
	"$KIT_IOINIT" "$KIT_RELOCS" "$OUT/"
kit_info "$OUT" id=z3660rtg version=$VERSION major=55 node=/dev/z3660rtg line=040 init=z3660rtginit \
	"entries=z3660rtginit z3660rtgopen z3660rtgclose z3660rtgread z3660rtgwrite z3660rtgioctl z3660rtgmmap" \
	"provides=dev_kvmap dev_kvunmap" conflicts=zz9k_regoff_z3660 streams=no
kit_sums "$OUT" z3660rtg_040.o devkvmap040.o z3660rtg_pad.o patch_z3660rtg_cdevsw.py patch_io_init.py \
	check_relink_relocs.py KIT.info
rm -rf "$B"
echo "[OK] z3660rtg kit $VERSION (040) in $OUT"
cat "$OUT/KIT.info" "$OUT/KIT.sha256"
