#!/bin/sh
# relink-030-rtgfb.sh -- add /dev/rtgfb (src/rtgfb.c, major 54) to a STOCK
# 68030 AMIX 2.1 kernel: the kernel of an A3000 or A2500 with its own 68030,
# and of FS-UAE's A3000.  The 68040/68060 port kernels cannot run on a 68030
# (pvmk docs/A2500-030-EVALUATION.md section 2.1), so a 68030 machine with a
# graphics card gets its stock kernel plus this one driver.
#
# Nothing in the base is byte-patched: one ld -r and one relocation retarget
# in cdevsw[54] (src/patch_rtgfb_cdevsw.py, by symbol, asserting nodev), so
# any 2.1c or 2.1p2a /stand/unix serves as the base.
#   sh relink-030-rtgfb.sh IN OUT
# IN: the machine's own /stand/unix (stock, 2 KB pages).  OUT: the new kernel.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/tools/config-load.sh"
. "$HERE/tools/build-step.sh"
IN="$1"; OUT="$2"
[ -f "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 stock-030-kernel output"; exit 1; }
. "$HERE/tools/m68k-binutils.sh"
echo "[*] base: $IN"
echo "      sha256 $(sha256sum "$IN" | cut -d' ' -f1)"
# a 68040/68060 port kernel has cputype (pvmk's kernel_fits test): not this script's base
$NM "$IN" | grep -qE " [TDBC] cputype$" && { echo "[FAIL] base is a 68040/68060 port kernel: use relink-040-rtgfb.sh"; exit 1; }
$NM "$IN" | grep -qE " T rtgfbopen$" && { echo "[FAIL] base already has rtgfb"; exit 1; }
for s in autocon getminor copyin copyout bzero printf nodev cdevsw io_init parinit; do
	$NM "$IN" | grep -qE " [TDBC] $s\$" || { echo "[FAIL] base lacks $s"; exit 1; }
done

# the stock sysroot: 2 KB pages, as this kernel (no Model-B mirror here)
B="$HERE/build/rtgfb030"; rm -rf "$B"; mkdir -p "$B"
cp "$HERE/src/rtgfb.c" "$HERE/src/rtgfb.h" "$B/"
CF="$AMIX_KERNEL_CFLAGS -Dsigned=__signed__"
echo "[*] cross-compiling rtgfb.c for the 68030 kernel"
(cd "$B" && m68k-cbm-sysv4-gcc $CF -I. -c rtgfb.c -o rtgfb_030.o)
# the page shift must be the stock one: by 11 (2 KB), never by 12
D=$($OBJDUMP -d "$B/rtgfb_030.o")
n11=$(echo "$D" | grep -A1 "moveq #11," | grep -cE "lsrl|asrl" || true)
n12=$(echo "$D" | grep -A1 "moveq #12," | grep -cE "lsrl|asrl" || true)
[ "$n12" -eq 0 ] && [ "$n11" -gt 0 ] || { echo "[FAIL] rtgfb_030.o page shifts: $n11 by 11, $n12 by 12 (want only 11)"; exit 1; }
echo "[OK] $n11 page shift(s), all by 11 (2 KB pages)"
# 68020/68030 code only: no 68040/68060-only instructions in the object
if echo "$D" | grep -E "\s(move16|cinv[alp]|cpush[alp]|pflushan?|pflushn)\s"; then
	echo "[FAIL] rtgfb_030.o has 68040/68060 instructions"; exit 1; fi
# .data of the object padded to 4, so the kernel's .data stays a 4-byte multiple
ODS=$(( 0x$($READELF -SW "$B/rtgfb_030.o" | awk '{gsub(/[][]/,"")} $2==".data"{print $6}') ))
printf '\t.data\n\t.space %d\n' $(( (4 - ODS % 4) % 4 )) > "$B/rtgfb_pad.s"
m68k-cbm-sysv4-gcc -c "$B/rtgfb_pad.s" -o "$B/rtgfb_pad.o"
$OBJCOPY --set-section-alignment .data=1 "$B/rtgfb_pad.o"
m68k-cbm-sysv4-ld -r -o "$OUT" "$IN" "$B/rtgfb_030.o" "$B/rtgfb_pad.o"
for s in rtgfbinit rtgfbopen rtgfbclose rtgfbioctl rtgfbmmap; do
	[ "$($NM "$OUT" | grep -cE " T $s\$")" -eq 1 ] || { echo "[FAIL] $s: not exactly one strong definition"; exit 1; }
done
LEAK=$($NM "$OUT" | awk '$1=="U" && $2!="edata" && $2!="end" && $2!="etext" {print $2}')
[ -z "$LEAK" ] || { echo "[FAIL] unresolved:"; echo "$LEAK"; exit 1; }
echo "[OK] symbols defined, none unresolved"
run_step 8 python3 "$HERE/src/patch_rtgfb_cdevsw.py" "$OUT"
# the board named at boot: io_init[0] parinit -> rtgfbinit (which calls parinit)
run_step 2 python3 "$HERE/src/patch_rtgfb_init.py" "$OUT"
CONTIG=$($READELF -SW "$OUT" | awk '{gsub(/[][]/,"")} $2==".text"{to=$5;ts=$6} $2==".data"{do_=$5} END{print to, ts, do_}')
set -- $CONTIG; set -- $(( 0x$1 + 0x$2 )) $(( 0x$3 ))
[ "$1" = "$2" ] && echo "[OK] text/data contiguous." || { echo "[FAIL] text/data NOT contiguous"; exit 1; }
DSZ=$(( 0x$($READELF -SW "$OUT" | awk '{gsub(/[][]/,"")} $2==".data"{print $6}') ))
[ $((DSZ % 4)) -eq 0 ] && echo "[OK] .data 4-aligned" || { echo "[FAIL] .data misaligned"; exit 1; }
run_step 1 python3 "$HERE/src/check_relink_relocs.py" "$OUT"
rm -rf "$B"
echo "[OK] built $OUT ($(sha256sum "$OUT" | cut -d' ' -f1))"
