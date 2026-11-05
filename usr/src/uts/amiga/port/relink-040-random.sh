#!/bin/sh
# relink-040-random.sh -- add /dev/random and /dev/urandom (src/random.c,
# major 48, minors 0 and 1) to a 68040/68060 kernel.  The driver is a small
# non-blocking PRNG: it seeds itself at the first open from the clock, lbolt
# and the opener, stirs those in again for every read, and mixes
# in whatever is written to either node.  Usage:
#   sh relink-040-random.sh IN OUT
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/tools/config-load.sh"
. "$HERE/tools/build-step.sh"
IN="$1"; OUT="$2"
[ -f "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 base output"; exit 1; }
NM=m68k-linux-gnu-nm
$NM "$IN" | grep -qE " T randopen$" && { echo "[FAIL] base already has /dev/random"; exit 1; }
for s in hrestime lbolt uiomove getminor nodev cdevsw; do
	$NM "$IN" | grep -qE " [TDBC] $s\$" || { echo "[FAIL] base lacks $s"; exit 1; }
done
echo "[*] base: $(basename "$IN")"
sh "$HERE/src/mk_modelb_sysroot.sh" | sed 's/^/      /'
AMIX_SYSROOT="$HERE/build/sysroot-modelb"; export AMIX_SYSROOT
P="$HERE/build/randomsrc"; rm -rf "$P"; mkdir -p "$P"
cp "$HERE/src/random.c" "$P/"
CF=$(echo "$AMIX_KERNEL_CFLAGS" | sed 's/-m68020/-m68040/')
CF="$CF -Dsigned=__signed__"
echo "[*] cross-compiling random.c"
(cd "$P" && m68k-cbm-sysv4-gcc $CF -I. -I"$AMIX_ROOT/usr/sys/amiga/inc" -c random.c -o "$HERE/build/random_040.o")
# .data/.bss of the object: keep the kernel's .data a 4-byte multiple
ODS=$(( 0x$(m68k-linux-gnu-readelf -SW "$HERE/build/random_040.o" | awk '{gsub(/[][]/,"")} $2==".data"{print $6}') ))
printf '\t.data\n\t.space %d\n' $(( (4 - ODS % 4) % 4 )) > "$HERE/build/random_pad.s"
m68k-cbm-sysv4-gcc -c "$HERE/build/random_pad.s" -o "$HERE/build/random_pad.o"
m68k-linux-gnu-objcopy --set-section-alignment .data=1 "$HERE/build/random_pad.o"
sh "$HERE/src/check_page_geometry.sh" "$HERE/build/random_040.o" | sed 's/^/      /'
if m68k-linux-gnu-objdump -d "$HERE/build/random_040.o" | grep -E "(mul|div)[su]l[.a-z]*\s+[^,]*,%d[0-7]:%d[0-7]"; then
	echo "[FAIL] random_040.o has 64-bit mul/div forms (060 vector 61)"; exit 1; fi
echo "[OK] no 64-bit mul/div forms"
m68k-cbm-sysv4-ld -r -o "$OUT" "$IN" "$HERE/build/random_040.o" "$HERE/build/random_pad.o"
for s in randopen randclose randread randwrite; do
	[ "$($NM "$OUT" | grep -cE " T $s\$")" -eq 1 ] || { echo "[FAIL] $s: not exactly one strong definition"; exit 1; }
done
LEAK=$($NM "$OUT" | awk '$1=="U" && $2!="edata" && $2!="end" && $2!="etext" {print $2}')
[ -z "$LEAK" ] || { echo "[FAIL] unresolved:"; echo "$LEAK"; exit 1; }
echo "[OK] symbols defined, none unresolved"
run_step 8 python3 "$HERE/src/patch_random_cdevsw.py" "$OUT"
CONTIG=$(m68k-linux-gnu-readelf -SW "$OUT" | awk '{gsub(/[][]/,"")} $2==".text"{to=$5;ts=$6} $2==".data"{do_=$5} END{print to, ts, do_}')
set -- $CONTIG; set -- $(( 0x$1 + 0x$2 )) $(( 0x$3 ))
[ "$1" = "$2" ] && echo "[OK] text/data contiguous." || { echo "[FAIL] text/data NOT contiguous"; exit 1; }
DSZ=$(( 0x$(m68k-linux-gnu-readelf -SW "$OUT" | awk '{gsub(/[][]/,"")} $2==".data"{print $6}') ))
[ $((DSZ % 4)) -eq 0 ] && echo "[OK] .data 4-aligned" || { echo "[FAIL] .data misaligned"; exit 1; }
run_step 1 python3 "$HERE/src/check_relink_relocs.py" "$OUT"
python3 "$HERE/src/stamp_buildid.py" "$OUT"
echo "[OK] built $OUT"
