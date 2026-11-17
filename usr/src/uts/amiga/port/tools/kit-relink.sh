#!/bin/sh
# kit-relink.sh BASE OUT KIT... -- link driver kits into a kernel the way the
# AMIX Installer does (amix-installer kernel.py), for tests and by hand: line=030
# kits into a stock 68030 kernel, line=040 kits into a 68040/68060 port kernel.
#
#   sh kit-relink.sh /stand/unix-copy unix.rtgfb+hydra rtgfb/port/kit hydra/port/kit
#
# The kits in the order given are the chain order: graphics before network.
#  1. every kit's files against its KIT.sha256 (KIT.info listed too);
#  2. amixdrv_init.s from src/amixdrv_init.s.in: jsr parinit, then each kit's
#     KIT.info init= in order; amixdrv_int2.s from src/amixdrv_int2.s.in:
#     aenintr, then each kit's KIT.info intr= in order (only when a kit has
#     one); assembled with m68k-cbm-sysv4-as;
#  3. m68k-cbm-sysv4-ld -r BASE, every kit's *.o in KIT.sha256 order, the stubs;
#  4. every kit's patch_*.py in KIT.sha256 order, except patch_io_init.py and
#     patch_int2.py;
#  5. patch_int2.py OUT amixdrv_int2, once (when a kit has intr=);
#  6. patch_io_init.py OUT amixdrv_init, once;
#  7. checks: every KIT.info entries= symbol, init and intr, and amixdrv_init/
#     amixdrv_int2, defined exactly once; nothing unresolved; .text and .data
#     contiguous; .data a multiple of 4; check_relink_relocs.py (the loader's
#     relocation pass).
# BASE is only read.  Needs m68k-cbm-sysv4-{as,ld} and m68k ELF binutils.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/.." && pwd)
. "$PORT/tools/kit-lib.sh"
BASE=$1; OUT=$2
[ -f "$BASE" ] && [ -n "$OUT" ] && [ $# -ge 3 ] || { echo "usage: $0 BASE OUT KIT..."; exit 1; }
shift 2
command -v m68k-cbm-sysv4-ld >/dev/null || { echo "no m68k-cbm-sysv4-ld on PATH"; exit 1; }
for _p in m68k-elf m68k-linux-gnu; do
	command -v $_p-nm >/dev/null 2>&1 && { NM=$_p-nm; READELF=$_p-readelf; break; }
done
[ -n "$NM" ] || { echo "no m68k ELF binutils"; exit 1; }
info() { sed -n "s/^$2=//p" "$1/KIT.info"; }

LINE=$(info "$1" line)
if $NM "$BASE" | grep -qE " [TDBC] cputype$"; then
	[ "$LINE" = 040 ] || { echo "[FAIL] base is a 68040/68060 port kernel, the kits are line=$LINE"; exit 1; }
else
	[ "$LINE" = 030 ] || { echo "[FAIL] base is a stock 68030 kernel, the kits are line=$LINE"; exit 1; }
fi
$NM "$BASE" | grep -qE " T amixdrv_init$" && { echo "[FAIL] base already has a driver chain (amixdrv_init)"; exit 1; }
T=$(mktemp -d "${TMPDIR:-/tmp}/kit-relink.XXXXXX")
trap 'rm -rf "$T"' EXIT INT TERM
OBJS=; INITS=; INTRS=; ENTRIES=; IOP=; I2P=
for K in "$@"; do
	[ -f "$K/KIT.sha256" ] && [ -f "$K/KIT.info" ] || { echo "[FAIL] $K: no KIT.sha256/KIT.info"; exit 1; }
	grep -q " KIT.info$" "$K/KIT.sha256" || { echo "[FAIL] $K: KIT.info not in KIT.sha256"; exit 1; }
	(cd "$K" && sha256sum -c --quiet KIT.sha256) || { echo "[FAIL] $K: KIT.sha256"; exit 1; }
	[ "$(info "$K" line)" = "$LINE" ] || { echo "[FAIL] $K: line=$(info "$K" line), not $LINE"; exit 1; }
	for f in $(awk '$2 ~ /\.o$/ {print $2}' "$K/KIT.sha256"); do OBJS="$OBJS $K/$f"; done
	i=$(info "$K" init); [ -z "$i" ] || INITS="$INITS $i"
	r=$(info "$K" intr); [ -z "$r" ] || INTRS="$INTRS $r"
	ENTRIES="$ENTRIES $(info "$K" entries) $i $r"
	for e in $(info "$K" entries); do
		$NM "$BASE" | grep -qE " [TDBC] $e\$" && { echo "[FAIL] base already has $e"; exit 1; }
	done
	[ -n "$IOP" ] || [ ! -f "$K/patch_io_init.py" ] || IOP=$K/patch_io_init.py
	[ -n "$I2P" ] || [ ! -f "$K/patch_int2.py" ] || I2P=$K/patch_int2.py
	echo "[*] kit $(info "$K" id) $(info "$K" version): major $(info "$K" major), $(info "$K" node)"
done
[ -n "$IOP" ] || IOP=$KIT_IOINIT
[ -n "$I2P" ] || I2P=$KIT_INT2
if [ -n "$INITS" ]; then
	J=$(for i in $INITS; do printf '\\tjsr\\t%s\\n' "$i"; done)
	sed "s/^@INITS@\$/$J/" "$KIT_STUB" | sed '/^$/d' > "$T/amixdrv_init.s"
	m68k-cbm-sysv4-as -o "$T/amixdrv_init.o" "$T/amixdrv_init.s"
	OBJS="$OBJS $T/amixdrv_init.o"
	echo "[*] amixdrv_init: parinit,$(echo $INITS | sed 's/ /, /g; s/^/ /')"
fi
if [ -n "$INTRS" ]; then
	$NM "$BASE" | grep -qE " T amixdrv_int2$" && { echo "[FAIL] base already has a level-2 chain (amixdrv_int2)"; exit 1; }
	J=$(for r in $INTRS; do printf '\\tmovel\\t%%sp@(4),%%sp@-\\n\\tjsr\\t%s\\n\\taddql\\t\\&4,%%sp\\n' "$r"; done)
	sed "s/^@INTRS@\$/$J/" "$KIT_INT2STUB" | sed '/^$/d' > "$T/amixdrv_int2.s"
	m68k-cbm-sysv4-as -o "$T/amixdrv_int2.o" "$T/amixdrv_int2.s"
	OBJS="$OBJS $T/amixdrv_int2.o"
	echo "[*] amixdrv_int2: aenintr,$(echo $INTRS | sed 's/ /, /g; s/^/ /')"
fi
m68k-cbm-sysv4-ld -r -o "$OUT" "$BASE" $OBJS
for K in "$@"; do
	for p in $(awk '$2 ~ /^patch_.*\.py$/ && $2 != "patch_io_init.py" && $2 != "patch_int2.py" {print $2}' "$K/KIT.sha256"); do
		echo "[*] $p"; python3 "$K/$p" "$OUT"
	done
done
if [ -n "$INTRS" ]; then
	echo "[*] patch_int2.py amixdrv_int2"; python3 "$I2P" "$OUT" amixdrv_int2
	ENTRIES="$ENTRIES amixdrv_int2"
fi
if [ -n "$INITS" ]; then
	echo "[*] patch_io_init.py amixdrv_init"; python3 "$IOP" "$OUT" amixdrv_init
	ENTRIES="$ENTRIES amixdrv_init"
fi
for s in $ENTRIES; do
	n=$($NM "$OUT" | grep -cE " [TDBC] $s\$" || true)
	[ "$n" -eq 1 ] || { echo "[FAIL] $s: $n definitions, not one"; exit 1; }
done
LEAK=$($NM "$OUT" | awk '$1=="U" && $2!="edata" && $2!="end" && $2!="etext" {print $2}')
[ -z "$LEAK" ] || { echo "[FAIL] unresolved:"; echo "$LEAK"; exit 1; }
set -- $($READELF -SW "$OUT" | awk '{gsub(/[][]/,"")} $2==".text"{to=$5;ts=$6} $2==".data"{do_=$5;ds=$6} END{print to, ts, do_, ds}')
[ $(( 0x$1 + 0x$2 )) -eq $(( 0x$3 )) ] || { echo "[FAIL] .text and .data not contiguous"; exit 1; }
[ $(( 0x$4 % 4 )) -eq 0 ] || { echo "[FAIL] .data not a multiple of 4"; exit 1; }
echo "[OK] entries defined once, none unresolved, .text/.data contiguous, .data 4-aligned"
python3 "$KIT_RELOCS" "$OUT" > "$T/relocs.log" || { cat "$T/relocs.log"; echo "[FAIL] loader relocation check"; exit 1; }
tail -1 "$T/relocs.log"
echo "[OK] $OUT  sha256 $(sha256sum "$OUT" | cut -d' ' -f1)"
