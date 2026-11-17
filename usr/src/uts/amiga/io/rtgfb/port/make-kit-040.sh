#!/bin/sh
# make-kit-040.sh [OUTDIR] -- the AMIX Installer's rtgfb kit for the
# 68040/68060 PORT kernels (Model B, 4 KB pages): /dev/rtgfb (major 54), the
# Picasso II/II+ (Zorro II).  OUTDIR defaults to kit-040/ beside this script
# (gitignored).
#
#   rtgfb_040.o             src/rtgfb.c compiled -m68040 against the Model-B
#                           headers with
#                            -DRTGFB_KVA: the driver's register accesses
#                             through dev_kvmap noncacheable-serialised (the
#                             VGA index/data pairs in order), as va2000-040;
#                            -DRTGFB_FB_NC: the framebuffer registered
#                             noncacheable-not-serialised (hat_cm_fb_add);
#                           mmap(2) of both boards is unchanged (page frames)
#   devkvmap040.o           port/src/devkvmap040.s: dev_kvmap/dev_kvunmap,
#                           KIT.info provides=: left out by the relink when
#                           the base has them (the z3660 variant), and linked
#                           once when va2000-040 brings the same object
#   rtgfb_pad.o             pads the kernel's .data back to a multiple of 4
#   patch_rtgfb_cdevsw.py   cdevsw[54] (by symbol, asserting nodev)
#   patch_io_init.py        io_init[0] parinit -> amixdrv_init (port/src)
#   check_relink_relocs.py  the loader's relocation check
#   KIT.info (line=040), KIT.sha256
#
# The base must be a port kernel (cputype) with hat_cm_fb_add, with or
# without dev_kvmap.  The version follows the 030 kit (make-kit-030.sh).
# Kernel code is compiled with the gcc 2.7.2.3 cross compiler only.  Needs
# AMIX_ROOT and m68k-cbm-sysv4-gcc on PATH, as make-kit-030.sh.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
if [ -f "$HERE/tools/kit-lib.sh" ]; then PORT=$HERE; else PORT=$(cd "$HERE/../../../port" && pwd); fi
. "$PORT/tools/kit-lib.sh"
kit_env
OUT=${1:-$HERE/kit-040}
VERSION=0.2

B="$HERE/build/rtgfbkit040"; rm -rf "$B"; mkdir -p "$B/src"
cp "$HERE/src/rtgfb.c" "$HERE/src/rtgfb.h" "$B/src/"
kit_modelb_sysroot "$B"
(cd "$B/src" && AMIX_SYSROOT="$KIT_SYSROOT" m68k-cbm-sysv4-gcc $KIT_CF040 -Dsigned=__signed__ \
	-DRTGFB_KVA -DRTGFB_FB_NC -I. -c rtgfb.c -o "$B/rtgfb_040.o")
kit_check_040 "$B/rtgfb_040.o"
U=$($NM "$B/rtgfb_040.o" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
[ "$U" = "autocon bzero copyin copyout dev_kvmap hat_cm_fb_add printf " ] || {
	echo "[FAIL] rtgfb_040.o imports: $U"; exit 1; }
echo "[OK] imports: $U(-DRTGFB_KVA -DRTGFB_FB_NC took)"
m68k-cbm-sysv4-gcc -m68040 -c "$PORT/src/devkvmap040.s" -o "$B/devkvmap040.o"
kit_pad "$B/rtgfb_040.o" "$B/devkvmap040.o" "$B/rtgfb_pad.o"

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/rtgfb_040.o" "$B/devkvmap040.o" "$B/rtgfb_pad.o" "$HERE/src/patch_rtgfb_cdevsw.py" \
	"$KIT_IOINIT" "$KIT_RELOCS" "$OUT/"
kit_info "$OUT" id=rtgfb version=$VERSION major=54 node=/dev/rtgfb line=040 init=rtgfbinit \
	"entries=rtgfbinit rtgfbopen rtgfbclose rtgfbioctl rtgfbmmap" \
	"provides=dev_kvmap dev_kvunmap" streams=no
kit_sums "$OUT" rtgfb_040.o devkvmap040.o rtgfb_pad.o patch_rtgfb_cdevsw.py patch_io_init.py \
	check_relink_relocs.py KIT.info
rm -rf "$B"
echo "[OK] rtgfb kit $VERSION (040) in $OUT"
cat "$OUT/KIT.info" "$OUT/KIT.sha256"
