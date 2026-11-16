#!/bin/sh
# relink-040-rtgfb.sh -- add /dev/rtgfb (src/rtgfb.c, major 54) to a 68040/68060
# port kernel, as one more step of the relink chain (after random, like any
# driver).  On a kernel with the cache-class selector (hat_cm_fb_add) the
# framebuffer is registered noncacheable-not-serialised (-DRTGFB_FB_NC); the
# registers stay serialised.  Usage:
#   sh relink-040-rtgfb.sh IN OUT
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/tools/config-load.sh"
. "$HERE/tools/build-step.sh"
IN="$1"; OUT="$2"
[ -f "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 base output"; exit 1; }
. "$HERE/tools/m68k-binutils.sh"
$NM "$IN" | grep -qE " [TDBC] cputype$" || { echo "[FAIL] base is not a 68040/68060 port kernel: use relink-030-rtgfb.sh"; exit 1; }
$NM "$IN" | grep -qE " T rtgfbopen$" && { echo "[FAIL] base already has rtgfb"; exit 1; }
for s in autocon getminor copyin copyout bzero printf nodev cdevsw; do
	$NM "$IN" | grep -qE " [TDBC] $s\$" || { echo "[FAIL] base lacks $s"; exit 1; }
done
FBNC=
$NM "$IN" | grep -qE " T hat_cm_fb_add$" && FBNC=-DRTGFB_FB_NC
echo "[*] base: $(basename "$IN") ${FBNC:+(framebuffer NC: $FBNC)}"
sh "$HERE/src/mk_modelb_sysroot.sh" | sed 's/^/      /'
AMIX_SYSROOT="$HERE/build/sysroot-modelb"; export AMIX_SYSROOT
P="$HERE/build/rtgfbsrc"; rm -rf "$P"; mkdir -p "$P"
cp "$HERE/src/rtgfb.c" "$HERE/src/rtgfb.h" "$P/"
CF=$(echo "$AMIX_KERNEL_CFLAGS" | sed 's/-m68020/-m68040/')
CF="$CF -Dsigned=__signed__ $FBNC"
echo "[*] cross-compiling rtgfb.c"
(cd "$P" && m68k-cbm-sysv4-gcc $CF -I. -c rtgfb.c -o "$HERE/build/rtgfb_040.o")
# .data/.bss of the object: keep the kernel's .data a 4-byte multiple
ODS=$(( 0x$($READELF -SW "$HERE/build/rtgfb_040.o" | awk '{gsub(/[][]/,"")} $2==".data"{print $6}') ))
printf '\t.data\n\t.space %d\n' $(( (4 - ODS % 4) % 4 )) > "$HERE/build/rtgfb_pad.s"
m68k-cbm-sysv4-gcc -c "$HERE/build/rtgfb_pad.s" -o "$HERE/build/rtgfb_pad.o"
$OBJCOPY --set-section-alignment .data=1 "$HERE/build/rtgfb_pad.o"
sh "$HERE/src/check_page_geometry.sh" "$HERE/build/rtgfb_040.o" | sed 's/^/      /'
if $OBJDUMP -d "$HERE/build/rtgfb_040.o" | grep -E "(mul|div)[su]l[.a-z]*\s+[^,]*,%d[0-7]:%d[0-7]"; then
	echo "[FAIL] rtgfb_040.o has 64-bit mul/div forms (060 vector 61)"; exit 1; fi
echo "[OK] no 64-bit mul/div forms"
m68k-cbm-sysv4-ld -r -o "$OUT" "$IN" "$HERE/build/rtgfb_040.o" "$HERE/build/rtgfb_pad.o"
for s in rtgfbopen rtgfbclose rtgfbioctl rtgfbmmap; do
	[ "$($NM "$OUT" | grep -cE " T $s\$")" -eq 1 ] || { echo "[FAIL] $s: not exactly one strong definition"; exit 1; }
done
LEAK=$($NM "$OUT" | awk '$1=="U" && $2!="edata" && $2!="end" && $2!="etext" {print $2}')
[ -z "$LEAK" ] || { echo "[FAIL] unresolved:"; echo "$LEAK"; exit 1; }
echo "[OK] symbols defined, none unresolved"
run_step 8 python3 "$HERE/src/patch_rtgfb_cdevsw.py" "$OUT"
CONTIG=$($READELF -SW "$OUT" | awk '{gsub(/[][]/,"")} $2==".text"{to=$5;ts=$6} $2==".data"{do_=$5} END{print to, ts, do_}')
set -- $CONTIG; set -- $(( 0x$1 + 0x$2 )) $(( 0x$3 ))
[ "$1" = "$2" ] && echo "[OK] text/data contiguous." || { echo "[FAIL] text/data NOT contiguous"; exit 1; }
DSZ=$(( 0x$($READELF -SW "$OUT" | awk '{gsub(/[][]/,"")} $2==".data"{print $6}') ))
[ $((DSZ % 4)) -eq 0 ] && echo "[OK] .data 4-aligned" || { echo "[FAIL] .data misaligned"; exit 1; }
run_step 1 python3 "$HERE/src/check_relink_relocs.py" "$OUT"
python3 "$HERE/src/stamp_buildid.py" "$OUT"
echo "[OK] built $OUT"
