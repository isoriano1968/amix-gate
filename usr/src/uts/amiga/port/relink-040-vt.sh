#!/bin/sh
# relink-040-vt.sh -- virtual terminals on the Z3660 RTG (src/vt040.c): Alt+F
# keys switch between the stock text consoles and an X session on the RTG,
# both ways.  In place of relink-040-c1hook.sh: the base must carry the zz9000
# driver with zz9000vtinfo/zz9000vtpan (amix-z3660rtg) and the stock
# scrcon_put.  Usage:
#   sh relink-040-vt.sh IN OUT
# DisplayScreen, SelectScreen and scrcon_put are weakened and kept as
# *_stock; vt040.o supplies the new ones.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/tools/config-load.sh"
. "$HERE/tools/build-step.sh"
IN="$1"; OUT="$2"
[ -f "$IN" ] && [ -n "$OUT" ] || { echo "usage: $0 base output"; exit 1; }
NM=m68k-linux-gnu-nm
for s in zz9000vtinfo zz9000vtpan zz9000vtdirty DisplayScreen SelectScreen scrcon_put \
	displayedscreen activescreen menuscreen screenstamp stampwaiting timeout wakeup bcopy; do
	$NM "$IN" | grep -qE " [TDBC] $s\$" || { echo "[FAIL] base lacks $s"; exit 1; }
done
$NM "$IN" | grep -qE " T scrcon_put_stock$" && { echo "[FAIL] base has the c1hook console hook: build from its input"; exit 1; }
$NM "$IN" | grep -qE " T DisplayScreen_stock$" && { echo "[FAIL] base already has vt040"; exit 1; }
echo "[*] base: $(basename "$IN")"

# every caller must reach the three functions through their symbols
for s in DisplayScreen SelectScreen scrcon_put; do
	n=$(m68k-linux-gnu-readelf -rW "$IN" | awk -v s=$s '$5==s' | wc -l)
	[ "$n" -gt 0 ] || { echo "[FAIL] no symbol relocation against $s"; exit 1; }
	echo "      [ok] $n references go through $s"
done

sh "$HERE/src/mk_modelb_sysroot.sh" | sed 's/^/      /'
AMIX_SYSROOT="$HERE/build/sysroot-modelb"; export AMIX_SYSROOT
P="$HERE/build/vtsrc"; rm -rf "$P"; mkdir -p "$P"
cp "$HERE/src/vt040.c" "$P/"
CF=$(echo "$AMIX_KERNEL_CFLAGS" | sed 's/-m68020/-m68040/')
echo "[*] cross-compiling vt040.c"
(cd "$P" && m68k-cbm-sysv4-gcc $CF -I. -I"$AMIX_ROOT/usr/sys/amiga/console" -I"$AMIX_ROOT/usr/sys/amiga/inc" \
	-c vt040.c -o "$HERE/build/vt040.o")
ODS=$(( 0x$(m68k-linux-gnu-readelf -SW "$HERE/build/vt040.o" | awk '{gsub(/[][]/,"")} $2==".data"{print $6}') ))
printf '\t.data\n\t.space %d\n' $(( (4 - ODS % 4) % 4 )) > "$HERE/build/vt_pad.s"
m68k-cbm-sysv4-gcc -c "$HERE/build/vt_pad.s" -o "$HERE/build/vt_pad.o"
m68k-linux-gnu-objcopy --set-section-alignment .data=1 "$HERE/build/vt_pad.o"
sh "$HERE/src/check_page_geometry.sh" "$HERE/build/vt040.o" | sed 's/^/      /'
if m68k-linux-gnu-objdump -d "$HERE/build/vt040.o" | grep -E "(mul|div)[su]l[.a-z]*\s+[^,]*,%d[0-7]:%d[0-7]"; then
	echo "[FAIL] vt040.o has 64-bit mul/div forms (060 vector 61)"; exit 1; fi
echo "[OK] no 64-bit mul/div forms"

STAGE="$HERE/build/unix-stage-vt"; cp "$IN" "$STAGE"
ARGS=""
for s in DisplayScreen SelectScreen scrcon_put; do
	A=$($NM "$IN" | awk -v s=$s '$2=="T" && $3==s{print $1}')
	ARGS="$ARGS --weaken-symbol $s --add-symbol ${s}_stock=.text:0x$A,function,global"
done
m68k-linux-gnu-objcopy $ARGS "$STAGE"
m68k-cbm-sysv4-ld -r -o "$OUT" "$STAGE" "$HERE/build/vt040.o" "$HERE/build/vt_pad.o"
for s in DisplayScreen SelectScreen scrcon_put; do
	[ "$($NM "$OUT" | grep -cE " T $s\$")" -eq 1 ] || { echo "[FAIL] $s not unique"; exit 1; }
	[ "$($NM "$OUT" | awk -v s=$s '$3==s{print $1}')" != "$($NM "$IN" | awk -v s=$s '$3==s{print $1}')" ] \
		|| { echo "[FAIL] $s still resolves to the stock body"; exit 1; }
	$NM "$OUT" | grep -qE " T ${s}_stock\$" || { echo "[FAIL] ${s}_stock missing"; exit 1; }
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
