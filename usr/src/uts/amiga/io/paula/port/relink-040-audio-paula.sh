#!/bin/sh
# relink-040-audio-paula.sh -- relink the stock Amiga audio driver (/dev/audio)
# of a 68040/68060 kernel with the /dev/paula exclusion added
# (src/patch_audio_paula.py: paula_busy, audio_nopen), as relink-040-paula.sh
# needs it.  Usage:
#   [AUDIO_SRC=<dir with audio.c audio.h>] sh relink-040-audio-paula.sh IN OUT
# The stock source (AMIX_ROOT/usr/sys/amiga/driver) is copied and patched at
# build time; it is not part of this repository.  The stock driver stays in
# the image as dead code: its global entry points are weakened, so every
# reference from outside it -- cdevsw (audioinfo), the level-4 interrupt
# (audiointr) -- binds to the rebuilt object.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/tools/config-load.sh"
. "$HERE/tools/build-step.sh"
IN="$1"; OUT="$2"
[ -f "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 base output"; exit 1; }
SRC="${AUDIO_SRC:-$AMIX_ROOT/usr/sys/amiga/driver}"
[ -f "$SRC/audio.c" ] || { echo "ERROR: audio.c not found in $SRC (set AUDIO_SRC)"; exit 1; }
NM=m68k-linux-gnu-nm
GLOBALS="audioinfo audioinit audiointr"
for s in $GLOBALS; do
	[ "$($NM "$IN" | grep -cE " [TD] $s\$")" -eq 1 ] || { echo "[FAIL] base: $s not a unique global"; exit 1; }
done
$NM "$IN" | grep -qE " [Ww] audioinfo$" && { echo "[FAIL] base already has a relinked audio driver"; exit 1; }
echo "[*] base: $(basename "$IN")"
sh "$HERE/src/mk_modelb_sysroot.sh" | sed 's/^/      /'
AMIX_SYSROOT="$HERE/build/sysroot-modelb"; export AMIX_SYSROOT
B="$HERE/build/audiosrc"; rm -rf "$B"; mkdir -p "$B"
cp "$SRC/audio.c" "$SRC/audio.h" "$B/"
# the Amiga memory.h, not libc's <memory.h> that the -I order would find first
cp "$AMIX_ROOT/usr/sys/amiga/inc/memory.h" "$B/"
# /dev/paula (relink-040-paula.sh) and /dev/audio exclude each other
run_step 1 python3 "$HERE/src/patch_audio_paula.py" "$B/audio.c"
CF=$(echo "$AMIX_KERNEL_CFLAGS" | sed 's/-m68020/-m68040/')
# -traditional knows no "signed"; audio.h and audiohw.h use "signed char"
CF="$CF -Dsigned=__signed__"
echo "[*] cross-compiling audio.c"
(cd "$B" && m68k-cbm-sysv4-gcc $CF -I. -I"$AMIX_ROOT/usr/sys/amiga/inc" -c audio.c -o "$HERE/build/audio_040.o")
# -traditional puts the driver's strings in .data (0x199 bytes); the kernel's
# .data must stay a 4-byte multiple (the loader puts .bss right after it), so
# a filler object, byte-aligned, tops it up
ODS=$(( 0x$(m68k-linux-gnu-readelf -SW "$HERE/build/audio_040.o" | awk '{gsub(/[][]/,"")} $2==".data"{print $6}') ))
printf '\t.data\n\t.space %d\n' $(( (4 - ODS % 4) % 4 )) > "$HERE/build/audio_pad.s"
m68k-cbm-sysv4-gcc -c "$HERE/build/audio_pad.s" -o "$HERE/build/audio_pad.o"
m68k-linux-gnu-objcopy --set-section-alignment .data=1 "$HERE/build/audio_pad.o"
sh "$HERE/src/check_page_geometry.sh" "$HERE/build/audio_040.o" | sed 's/^/      /'
if m68k-linux-gnu-objdump -d "$HERE/build/audio_040.o" | grep -E "(mul|div)[su]l[.a-z]*\s+[^,]*,%d[0-7]:%d[0-7]"; then
	echo "[FAIL] audio_040.o has 64-bit mul/div forms (060 vector 61)"; exit 1; fi
SR=$(m68k-linux-gnu-objdump -d "$HERE/build/audio_040.o" | grep -cE ',%sr$')
[ "$SR" -gt 0 ] || { echo "[FAIL] audio_040.o writes %sr nowhere: spl()/splx() lost"; exit 1; }
echo "[OK] no 64-bit mul/div forms; $SR SR writes (spl/splx present)"
STAGE="$HERE/build/unix-stage-audio"; cp "$IN" "$STAGE"
W=""; for s in $GLOBALS; do W="$W --weaken-symbol $s"; done
m68k-linux-gnu-objcopy $W "$STAGE"
m68k-cbm-sysv4-ld -r -o "$OUT" "$STAGE" "$HERE/build/audio_040.o" "$HERE/build/audio_pad.o"
for s in $GLOBALS; do
	[ "$($NM "$OUT" | grep -cE " [TD] $s\$")" -eq 1 ] || { echo "[FAIL] $s: not exactly one strong definition"; exit 1; }
done
LEAK=$($NM "$OUT" | awk '$1=="U" && $2!="edata" && $2!="end" && $2!="etext" {print $2}')
[ -z "$LEAK" ] || { echo "[FAIL] unresolved:"; echo "$LEAK"; exit 1; }
echo "[OK] symbols defined, none unresolved"
CONTIG=$(m68k-linux-gnu-readelf -SW "$OUT" | awk '{gsub(/[][]/,"")} $2==".text"{to=$5;ts=$6} $2==".data"{do_=$5} END{print to, ts, do_}')
set -- $CONTIG; set -- $(( 0x$1 + 0x$2 )) $(( 0x$3 ))
[ "$1" = "$2" ] && echo "[OK] text/data contiguous." || { echo "[FAIL] text/data NOT contiguous"; exit 1; }
DSZ=$(( 0x$(m68k-linux-gnu-readelf -SW "$OUT" | awk '{gsub(/[][]/,"")} $2==".data"{print $6}') ))
[ $((DSZ % 4)) -eq 0 ] && echo "[OK] .data 4-aligned" || { echo "[FAIL] .data misaligned"; exit 1; }
run_step 1 python3 "$HERE/src/check_relink_relocs.py" "$OUT"
python3 "$HERE/src/stamp_buildid.py" "$OUT"
echo "[OK] built $OUT"
