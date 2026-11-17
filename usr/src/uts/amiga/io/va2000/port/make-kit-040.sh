#!/bin/sh
# make-kit-040.sh [OUTDIR] -- the va2000 kit for the 68040/68060 PORT kernels
# (Model B, 4 KB pages), from the same pieces the port's relink-040-va2000.sh
# uses.  OUTDIR defaults to kit-040/ beside this script (gitignored).
#
#   va2000_040.o            va2000.c (pinned) through the port's
#                           src/va2000_modelb.py (va2000mmap >> 11 -> >> 12),
#                           then patches/,
#                           compiled -m68040 against the Model-B headers
#                           (port/include-modelb) with -DVA2000_KVA: registers
#                           through dev_kvmap, framebuffer via hat_cm_fb_add,
#                           Zorro II and Zorro III
#   devkvmap040.o           port/src/devkvmap040.s: dev_kvmap/dev_kvunmap
#   va2000_pad.o            pads the kernel's .data back to a multiple of 4
#   patch_va2000_cdevsw.py  cdevsw[68] (by symbol, asserting nodev)
#   patch_io_init.py        io_init[0] parinit -> amixdrv_init (port/src)
#   check_relink_relocs.py  the loader's relocation check
#   KIT.info (line=040), KIT.sha256
#
# Unlike relink-040-va2000.sh it does not wrap parinit (weaken + parinit_orig):
# va2000init runs from the amixdrv_init chain like every kit's init.  The base
# must be a port kernel (cputype) with hat_cm_fb_add and without dev_kvmap.
#
# Kernel code is compiled with the gcc 2.7.2.3 cross compiler only.  Needs
# AMIX_ROOT and m68k-cbm-sysv4-gcc on PATH, as make-kit-030.sh.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../../../port" && pwd)
. "$PORT/tools/kit-lib.sh"
. "$HERE/fetch-source.sh"
kit_env
OUT=${1:-$HERE/kit-040}
VERSION=0.1

B="$HERE/build/va2000kit040"; rm -rf "$B"; mkdir -p "$B"
va2000_fetch "$B"
# the Model-B header set: the cross compiler's sysroot mirrored, with sys/immu.h
# and sys/param.h from port/include-modelb (as src/mk_modelb_sysroot.sh does)
GCC=$(command -v m68k-cbm-sysv4-gcc)
REAL=$(cd "$(dirname "$GCC")/.." && pwd)/m68k-cbm-sysv4/sysroot
[ -d "$REAL/usr/include/sys" ] || { echo "no cross sysroot at $REAL" >&2; exit 1; }
SR="$B/sysroot-modelb"; mkdir -p "$SR/usr/include/sys"
for e in "$REAL"/usr/*; do [ "$(basename "$e")" = include ] || ln -s "$e" "$SR/usr/"; done
for e in "$REAL"/usr/include/*; do [ "$(basename "$e")" = sys ] || ln -s "$e" "$SR/usr/include/"; done
for e in "$REAL"/usr/include/sys/*; do ln -s "$e" "$SR/usr/include/sys/"; done
for h in immu param; do
	rm -f "$SR/usr/include/sys/$h.h"
	ln -s "$REAL/usr/include/sys/$h.h" "$SR/usr/include/sys/${h}_stock.h"
	cp "$PORT/include-modelb/sys/$h.h" "$SR/usr/include/sys/"
done
CF=$(echo "$AMIX_KERNEL_CFLAGS" | sed 's/-m68020/-m68040/')
AMIX_SYSROOT="$SR" m68k-cbm-sysv4-gcc $CF -c "$PORT/src/modelb_geom_probe.c" -o "$B/probe.o" || {
	echo "[FAIL] the Model-B geometry probe does not compile through $SR"; exit 1; }
if AMIX_SYSROOT="$REAL" m68k-cbm-sysv4-gcc $CF -c "$PORT/src/modelb_geom_probe.c" -o /dev/null 2>/dev/null; then
	echo "[FAIL] the geometry probe compiles against the stock sysroot too: it proves nothing"; exit 1; fi
echo "[OK] Model-B headers (port/include-modelb) in the compile"
# va2000_modelb.py writes PORT/build/va2000_040.c (+ va2000.h): the port's own step
mkdir -p "$PORT/build"
VA2000_SRC="$VA2000_TREE/src/va2000.c" python3 "$PORT/src/va2000_modelb.py"
mv "$PORT/build/va2000_040.c" "$PORT/build/va2000.h" "$B/"
va2000_patch "$B/va2000_040.c"
(cd "$B" && AMIX_SYSROOT="$SR" m68k-cbm-sysv4-gcc $CF -Dsigned=__signed__ -DVA2000_KVA -I. \
	-c va2000_040.c -o va2000_040.o)
OBJDUMP=$OBJDUMP sh "$PORT/src/check_page_geometry.sh" "$B/va2000_040.o"
if $OBJDUMP -d "$B/va2000_040.o" | grep -E "(mul|div)[su]l[.a-z]*\s+[^,]*,%d[0-7]:%d[0-7]"; then
	echo "[FAIL] va2000_040.o has 64-bit mul/div forms (060 vector 61)"; exit 1; fi
U=$($NM "$B/va2000_040.o" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
[ "$U" = "autocon copyin copyout dev_kvmap dev_kvunmap hat_cm_fb_add printf uiomove " ] || {
	echo "[FAIL] va2000_040.o imports: $U"; exit 1; }
echo "[OK] imports: $U(-DVA2000_KVA took)"
m68k-cbm-sysv4-gcc -m68040 -c "$PORT/src/devkvmap040.s" -o "$B/devkvmap040.o"
kit_pad "$B/va2000_040.o" "$B/devkvmap040.o" "$B/va2000_pad.o"

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/va2000_040.o" "$B/devkvmap040.o" "$B/va2000_pad.o" "$HERE/src/patch_va2000_cdevsw.py" \
	"$KIT_IOINIT" "$KIT_RELOCS" "$OUT/"
cp "$VA2000_TREE/src/LICENSE" "$OUT/LICENSE.va2000"
kit_info "$OUT" id=va2000 version=$VERSION major=68 node=/dev/va2000 line=040 init=va2000init \
	"entries=va2000init va2000open va2000close va2000read va2000write va2000ioctl va2000mmap dev_kvmap dev_kvunmap" \
	streams=no
kit_sums "$OUT" va2000_040.o devkvmap040.o va2000_pad.o patch_va2000_cdevsw.py patch_io_init.py \
	check_relink_relocs.py LICENSE.va2000 KIT.info
rm -rf "$B"
echo "[OK] va2000 kit $VERSION (040) in $OUT"
cat "$OUT/KIT.info" "$OUT/KIT.sha256"
