#!/bin/sh
# relink-040-paula.sh -- add /dev/paula (src/paula.c, major 53) to a 68040/68060
# kernel whose audio driver defines paula_busy and audio_nopen
# (relink-040-audio-paula.sh, src/patch_audio_paula.py).  /dev/paula gives one program
# Paula's four channels register by register, replayed from CIA-A timer B;
# src/paula.c explains why and how.  Usage:
#   sh relink-040-paula.sh IN OUT
# aciaaintr (the level-2 CIA-A handler) is weakened: paula.o's version does
# the stock keyboard / parallel / clock work and then timer B.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/tools/config-load.sh"
. "$HERE/tools/build-step.sh"
IN="$1"; OUT="$2"
[ -f "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 base output"; exit 1; }
NM=m68k-linux-gnu-nm
$NM "$IN" | grep -qE " [BCD] paula_busy$" || { echo "[FAIL] base has no paula_busy -- run relink-040-audio-paula.sh first"; exit 1; }
$NM "$IN" | grep -qE " [BCD] audio_nopen$" || { echo "[FAIL] base has no audio_nopen"; exit 1; }
$NM "$IN" | grep -qE " T paulaopen$" && { echo "[FAIL] base already has /dev/paula"; exit 1; }
[ "$($NM "$IN" | grep -cE ' T aciaaintr$')" -eq 1 ] || { echo "[FAIL] base: aciaaintr not a unique global"; exit 1; }
for s in kbintr parintr clock_int addupc_clk displaytype AllocMem FreeMem; do
	$NM "$IN" | grep -qE " [TDBC] $s\$" || { echo "[FAIL] base lacks $s"; exit 1; }
done
echo "[*] base: $(basename "$IN")"
sh "$HERE/src/mk_modelb_sysroot.sh" | sed 's/^/      /'
AMIX_SYSROOT="$HERE/build/sysroot-modelb"; export AMIX_SYSROOT
P="$HERE/build/paulasrc"; rm -rf "$P"; mkdir -p "$P"
cp "$HERE/src/paula.c" "$HERE/src/paula.h" "$P/"
# the Amiga memory.h (AllocMem), not libc's <memory.h> that the -I order would find first
cp "$AMIX_ROOT/usr/sys/amiga/inc/memory.h" "$P/"
CF=$(echo "$AMIX_KERNEL_CFLAGS" | sed 's/-m68020/-m68040/')
CF="$CF -Dsigned=__signed__"
echo "[*] cross-compiling paula.c"
(cd "$P" && m68k-cbm-sysv4-gcc $CF -I. -I"$AMIX_ROOT/usr/sys/amiga/inc" -c paula.c -o "$HERE/build/paula_040.o")
# .data/.bss of the object: keep the kernel's .data a 4-byte multiple
ODS=$(( 0x$(m68k-linux-gnu-readelf -SW "$HERE/build/paula_040.o" | awk '{gsub(/[][]/,"")} $2==".data"{print $6}') ))
printf '\t.data\n\t.space %d\n' $(( (4 - ODS % 4) % 4 )) > "$HERE/build/paula_pad.s"
m68k-cbm-sysv4-gcc -c "$HERE/build/paula_pad.s" -o "$HERE/build/paula_pad.o"
m68k-linux-gnu-objcopy --set-section-alignment .data=1 "$HERE/build/paula_pad.o"
sh "$HERE/src/check_page_geometry.sh" "$HERE/build/paula_040.o" | sed 's/^/      /'
if m68k-linux-gnu-objdump -d "$HERE/build/paula_040.o" | grep -E "(mul|div)[su]l[.a-z]*\s+[^,]*,%d[0-7]:%d[0-7]"; then
	echo "[FAIL] paula_040.o has 64-bit mul/div forms (060 vector 61)"; exit 1; fi
SR=$(m68k-linux-gnu-objdump -d "$HERE/build/paula_040.o" | grep -cE ',%sr$')
[ "$SR" -gt 0 ] || { echo "[FAIL] paula_040.o writes %sr nowhere: spl4()/splx() lost"; exit 1; }
echo "[OK] no 64-bit mul/div forms; $SR SR writes (spl/splx present)"
STAGE="$HERE/build/unix-stage-paula"; cp "$IN" "$STAGE"
m68k-linux-gnu-objcopy --weaken-symbol aciaaintr "$STAGE"
m68k-cbm-sysv4-ld -r -o "$OUT" "$STAGE" "$HERE/build/paula_040.o" "$HERE/build/paula_pad.o"
for s in paulaopen paulaclose paulaioctl aciaaintr; do
	[ "$($NM "$OUT" | grep -cE " T $s\$")" -eq 1 ] || { echo "[FAIL] $s: not exactly one strong definition"; exit 1; }
done
LEAK=$($NM "$OUT" | awk '$1=="U" && $2!="edata" && $2!="end" && $2!="etext" {print $2}')
[ -z "$LEAK" ] || { echo "[FAIL] unresolved:"; echo "$LEAK"; exit 1; }
echo "[OK] symbols defined, none unresolved"
run_step 8 python3 "$HERE/src/patch_paula_cdevsw.py" "$OUT"
CONTIG=$(m68k-linux-gnu-readelf -SW "$OUT" | awk '{gsub(/[][]/,"")} $2==".text"{to=$5;ts=$6} $2==".data"{do_=$5} END{print to, ts, do_}')
set -- $CONTIG; set -- $(( 0x$1 + 0x$2 )) $(( 0x$3 ))
[ "$1" = "$2" ] && echo "[OK] text/data contiguous." || { echo "[FAIL] text/data NOT contiguous"; exit 1; }
DSZ=$(( 0x$(m68k-linux-gnu-readelf -SW "$OUT" | awk '{gsub(/[][]/,"")} $2==".data"{print $6}') ))
[ $((DSZ % 4)) -eq 0 ] && echo "[OK] .data 4-aligned" || { echo "[FAIL] .data misaligned"; exit 1; }
run_step 1 python3 "$HERE/src/check_relink_relocs.py" "$OUT"
python3 "$HERE/src/stamp_buildid.py" "$OUT"
echo "[OK] built $OUT"
