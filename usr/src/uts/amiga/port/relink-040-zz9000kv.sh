#!/bin/sh
# relink-040-zz9000.sh -- add the Z3660 RTG driver (amix-z3660rtg, zz9000.c with
# -DZZ9K_TARGET_Z3660) to a kernel that already carries the Z3660 SCSI/net drivers.
# Local script (isoriano, 2026-10-16), the shape given in amix-z3660rtg DESIGN.md 11.2:
#   cdevsw[49] -> zz9000*, a second parinit wrapper chained over the z3660 one,
#   Model-B sysroot, page-geometry check, no 64-bit mul/div in the object (vector 61).
#   usage: sh relink-040-zz9000.sh build/unix-040-z3660 build/unix-040-z3660-rtg
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/tools/config-load.sh"
. "$HERE/tools/build-step.sh"
IN="$1"; OUT="$2"
[ -f "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 base-with-z3660 output"; exit 1; }
RTG="${Z3660_RTG_SRC:-$HERE/../amix-z3660rtg/overlay/usr/sys/amiga/driver/zz9000}"
[ -f "$RTG/zz9000.c" ] || { echo "ERROR: zz9000.c not found in $RTG"; exit 1; }
NM=m68k-linux-gnu-nm
$NM "$IN" | grep -qE " T z3660queue$" || { echo "[FAIL] base has no Z3660 drivers -- use relink-040-z3660.sh output"; exit 1; }
$NM "$IN" | grep -qE " T zz9000init$" && { echo "[FAIL] base already has zz9000"; exit 1; }
PAR=$($NM "$IN" | awk '$2=="T" && $3=="parinit"{print $1}')
[ -n "$PAR" ] && [ "$PAR" != "0000fe6c" ] || { echo "[FAIL] parinit is not the z3660 wrapper (got '$PAR')"; exit 1; }
echo "[*] base: $(basename "$IN"), z3660 parinit wrapper @0x$PAR"

sh "$HERE/src/mk_modelb_sysroot.sh" | sed 's/^/      /'
AMIX_SYSROOT="$HERE/build/sysroot-modelb"; export AMIX_SYSROOT

# zz9000.c includes ../../console/*.h: stage it at amiga/driver/zz9000 next to the console headers.
Z="$HERE/build/zz9000src"; rm -rf "$Z"; mkdir -p "$Z/amiga/driver/zz9000"
cp "$RTG/zz9000.c" "$RTG/zz9000.h" "$Z/amiga/driver/zz9000/"
ln -s "$AMIX_ROOT/usr/sys/amiga/console" "$Z/amiga/console"
CF=$(echo "$AMIX_KERNEL_CFLAGS" | sed 's/-m68020/-m68040/')
echo "[*] cross-compiling zz9000.c (ZZ9K_TARGET_Z3660, ZZ9K_KVMAP: board above 1 GB mapped via dev_kvmap)"
(cd "$Z/amiga/driver/zz9000" && m68k-cbm-sysv4-gcc $CF -DZZ9K_TARGET_Z3660 -DZZ9K_KVMAP \
	-I"$AMIX_ROOT/usr/sys/amiga/inc" -c zz9000.c -o "$HERE/build/zz9000_040.o")
$NM "$HERE/build/zz9000_040.o" | grep -qE ' U dev_kvmap$' \
	|| { echo "[FAIL] zz9000_040.o does not import dev_kvmap -- built without -DZZ9K_KVMAP?"; exit 1; }
$NM "$IN" | grep -qE " T dev_kvmap$" || { echo "[FAIL] base has no dev_kvmap (relink-040-z3660.sh links it)"; exit 1; }
# the console font: AMIX's own console/sunfont.o (assembled on AMIX), which
# defines builtinfont[]; the base kernel has none (DESIGN.md 11.2)
FONT="${SUNFONT_O:-$AMIX_ROOT/usr/sys/amiga/console/sunfont.o}"
[ -f "$FONT" ] || { echo "ERROR: sunfont.o not found: $FONT (set SUNFONT_O)"; exit 1; }
$NM "$FONT" | grep -qE " T builtinfont$" || { echo "[FAIL] $FONT does not define builtinfont"; exit 1; }
$NM "$IN" | grep -qE " [TDB] builtinfont$" && { echo "[FAIL] base already defines builtinfont"; exit 1; }
cp "$FONT" "$HERE/build/sunfont.o"
echo "[OK] console font: $FONT"
sh "$HERE/src/check_page_geometry.sh" "$HERE/build/zz9000_040.o" | sed 's/^/      /'
# 68060: 64-bit mulsl/mulul/divsl/divul (Dh:Dl forms) trap to vector 61 -- refuse them in kernel code.
if m68k-linux-gnu-objdump -d "$HERE/build/zz9000_040.o" | grep -E "(mul|div)[su]l[.a-z]*\s+[^,]*,%d[0-7]:%d[0-7]"; then
	echo "[FAIL] zz9000_040.o has 64-bit mul/div forms (060 vector 61)"; exit 1; fi
echo "[OK] no 64-bit mul/div forms"
m68k-cbm-sysv4-gcc -m68040 -c "$HERE/src/parinit_zz9000.s" -o "$HERE/build/parinit_zz9000.o"

STAGE="$HERE/build/unix-stage-zz9000"; cp "$IN" "$STAGE"
m68k-linux-gnu-objcopy --weaken-symbol parinit \
	--add-symbol parinit_z3660=.text:0x$PAR,function,global "$STAGE"
echo "[*] ld -r: base + zz9000_040.o + parinit_zz9000.o (wrapper last: .balign 4)"
m68k-cbm-sysv4-ld -r -o "$OUT" "$STAGE" "$HERE/build/zz9000_040.o" "$HERE/build/sunfont.o" \
	"$HERE/build/parinit_zz9000.o"
for s in zz9000init zz9000open zz9000close zz9000read zz9000write zz9000ioctl zz9000mmap parinit parinit_z3660 parinit_orig z3660queue builtinfont; do
	$NM "$OUT" | grep -qE " [A-Za-z] $s\$" || { echo "[FAIL] $s missing"; exit 1; }
done
[ "$($NM "$OUT" | grep -cE ' T parinit$')" -eq 1 ] || { echo "[FAIL] parinit not unique"; exit 1; }
LEAK=$($NM "$OUT" | awk '$1=="U" && $2!="edata" && $2!="end" && $2!="etext" {print $2}')
[ -z "$LEAK" ] || { echo "[FAIL] unresolved:"; echo "$LEAK"; exit 1; }
echo "[OK] symbols defined, none unresolved"
run_step 8 python3 "$HERE/src/patch_zz9000_cdevsw.py" "$OUT"
CONTIG=$(m68k-linux-gnu-readelf -SW "$OUT" | awk '{gsub(/[][]/,"")} $2==".text"{to=$5;ts=$6} $2==".data"{do_=$5} END{print to, ts, do_}')
set -- $CONTIG; set -- $(( 0x$1 + 0x$2 )) $(( 0x$3 ))
[ "$1" = "$2" ] && echo "[OK] text/data contiguous." || { echo "[FAIL] text/data NOT contiguous"; exit 1; }
DSZ=$(( 0x$(m68k-linux-gnu-readelf -SW "$OUT" | awk '{gsub(/[][]/,"")} $2==".data"{print $6}') ))
[ $((DSZ % 4)) -eq 0 ] && echo "[OK] .data 4-aligned" || { echo "[FAIL] .data misaligned"; exit 1; }
run_step indent python3 "$HERE/src/patch_b2_flip.py" "$OUT" --check
run_step 1 python3 "$HERE/src/check_relink_relocs.py" "$OUT"
python3 "$HERE/src/stamp_buildid.py" "$OUT"
echo "[OK] built $OUT"
echo "     nodes: mknod /dev/zz9000 c 49 0   (RTG)   mknod /dev/zen0 c 51 0   (ethernet)"
