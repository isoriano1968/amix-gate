# kit-lib.sh -- sourced by the drivers' make-kit scripts (io/<driver>/port/
# make-kit-030.sh, make-kit-040.sh) and make-kit-tools.sh: the pieces every
# AMIX Installer kernel kit shares.
#
#   PORT=/path/to/amix-gate/usr/src/uts/amiga/port; . "$PORT/tools/kit-lib.sh"
#
# kit_env              AMIX_KERNEL_CFLAGS (a port checkout's config-load.sh,
#                      else $AMIX_ROOT) and the m68k ELF binutils for the checks
# kit_check_030 OBJ    68020/68030 code, no page shift other than by 11 (2 KB)
# kit_pad OBJ... PAD   a .data pad object: the objects' .data to a multiple of 4
# kit_info DIR k=v...  KIT.info
# kit_sums DIR FILE... KIT.sha256 over FILE... in that order (KIT.info included)
#
# Kernel code is compiled and linked only with the gcc 2.7.2.3 cross tools
# (m68k-cbm-sysv4-gcc/-as/-ld); the m68k-elf (or m68k-linux-gnu) binutils only
# read objects and set one section alignment.

[ -n "$PORT" ] && [ -f "$PORT/src/check_relink_relocs.py" ] || {
	echo "kit-lib.sh: PORT must name amix-gate's uts/amiga/port" >&2; exit 1; }
KIT_RELOCS="$PORT/src/check_relink_relocs.py"
KIT_IOINIT="$PORT/src/patch_io_init.py"
KIT_STUB="$PORT/src/amixdrv_init.s.in"
# the level-2 interrupt chain: kits with KIT.info intr= ship KIT_INT2
KIT_INT2="$PORT/src/patch_int2.py"
KIT_INT2STUB="$PORT/src/amixdrv_int2.s.in"

kit_env() {
	if [ -f "$PORT/tools/config-load.sh" ] && [ -f "$PORT/config.sh" ]; then
		. "$PORT/tools/config-load.sh"
	else
		[ -d "${AMIX_ROOT:-}/usr/sys/amiga/inc" ] || {
			echo "AMIX_ROOT must name an AMIX 2.1c tree with usr/include and usr/sys" >&2
			exit 1; }
		# the flags of gcc-cross-amix's env.sh, which the port's relinks use
		AMIX_KERNEL_CFLAGS="-O -traditional -fno-builtin -Dinline=__inline__ -Dvolatile=__volatile__ -Dconst=__const__ -m68020 -D_KERNEL -DSVR40 -DSVR4 -I$AMIX_ROOT/usr/include -I$AMIX_ROOT/usr/sys -I$AMIX_ROOT/usr/sys/amiga/inc"
	fi
	command -v m68k-cbm-sysv4-gcc >/dev/null || {
		echo "no m68k-cbm-sysv4-gcc on PATH (developer/amix/gcc-27)" >&2; exit 1; }
	OBJDUMP=
	for _p in m68k-elf m68k-linux-gnu; do
		if command -v $_p-objdump >/dev/null 2>&1 && command -v $_p-readelf >/dev/null 2>&1 &&
		   command -v $_p-objcopy >/dev/null 2>&1 && command -v $_p-nm >/dev/null 2>&1; then
			OBJDUMP=$_p-objdump; READELF=$_p-readelf; OBJCOPY=$_p-objcopy; NM=$_p-nm
			break
		fi
	done
	[ -n "$OBJDUMP" ] || {
		echo "no m68k ELF binutils (m68k-elf-* from developer/amix/gcc-14, or m68k-linux-gnu-*)" >&2
		exit 1; }
	$OBJCOPY --help 2>&1 | grep -q set-section-alignment || {
		echo "$OBJCOPY has no --set-section-alignment (binutils too old)" >&2; exit 1; }
}

# the stock kernel's page shift is 11 (2 KB): no shift by 12 in the object, and
# no 68040/68060-only instruction
kit_check_030() {
	_d=$($OBJDUMP -d "$1")
	_n11=$(echo "$_d" | grep -A1 "moveq #11," | grep -cE "lsrl|asrl" || true)
	_n12=$(echo "$_d" | grep -A1 "moveq #12," | grep -cE "lsrl|asrl" || true)
	[ "$_n12" -eq 0 ] || {
		echo "[FAIL] $(basename "$1"): $_n12 page shift(s) by 12 (stock pages are 2 KB)"; exit 1; }
	if echo "$_d" | grep -E "\s(move16|cinv[alp]|cpush[alp]|pflushan?|pflushn|plpa[rw])\s"; then
		echo "[FAIL] $(basename "$1") has 68040/68060 instructions"; exit 1; fi
	echo "[OK] $(basename "$1"): 68030 code, $_n11 page shift(s) by 11, none by 12"
}

# kit_pad OBJ... PAD: PAD gets the .data bytes that bring the objects' .data
# to a multiple of 4 (each object's .data placed at its section alignment, as
# ld does), with section alignment 1 (it must add exactly that)
kit_pad() {
	_sum=0; _pad=
	for _o in "$@"; do _pad=$_o; done
	for _o in "$@"; do
		[ "$_o" = "$_pad" ] && break
		# each object's .data starts at its own alignment, even when empty
		_l=$($READELF -SW "$_o" | awk '{gsub(/[][]/,"")} $2==".data"{print $6 ":" $NF}')
		_s=${_l%%:*}; _al=${_l##*:}
		_sum=$(( (_sum + ${_al:-1} - 1) / ${_al:-1} * ${_al:-1} + 0x${_s:-0} ))
	done
	printf '\t.data\n\t.space %d\n' $(( (4 - _sum % 4) % 4 )) > "$_pad.s"
	m68k-cbm-sysv4-as -o "$_pad" "$_pad.s"
	rm -f "$_pad.s"
	$OBJCOPY --set-section-alignment .data=1 "$_pad"
}

# kit_info DIR key=value...
kit_info() {
	_dir=$1; shift
	: > "$_dir/KIT.info"
	for _kv in "$@"; do echo "$_kv" >> "$_dir/KIT.info"; done
}

# kit_sums DIR FILE...: the order is the installer's link order (.o) and run
# order (patch_*.py; patch_io_init.py is run once, last, by the installer)
kit_sums() {
	_dir=$1; shift
	(cd "$_dir" && sha256sum "$@" > KIT.sha256)
	_n=$(ls "$_dir" | grep -vc '^KIT.sha256$')
	[ "$_n" -eq $# ] || { echo "[FAIL] $_dir has files KIT.sha256 does not list"; exit 1; }
}

# kit_modelb_sysroot DIR: the Model-B header set for line=040 objects in DIR
# (the cross compiler's sysroot mirrored, sys/immu.h and sys/param.h from
# port/include-modelb, as src/mk_modelb_sysroot.sh does), proven by the
# geometry probe compiling through it and not through the stock sysroot.
# Sets KIT_SYSROOT and KIT_CF040: -I$KIT_SYSROOT/usr/include FIRST, then
# AMIX_KERNEL_CFLAGS with -m68040.  First, because AMIX_KERNEL_CFLAGS carries
# -I$AMIX_ROOT/usr/include and -I$AMIX_ROOT/usr/sys, which hold the stock
# sys/immu.h and sys/param.h: a compiler wrapper that adds AMIX_SYSROOT's
# headers with -idirafter (amix-userland developer/gcc-27) searches those
# -I dirs before the Model-B set, and the probe then fails.  Every line=040
# compile (the probe and the driver objects) uses KIT_CF040 with
# AMIX_SYSROOT=$KIT_SYSROOT.
kit_modelb_sysroot() {
	_gcc=$(command -v m68k-cbm-sysv4-gcc)
	_real=$(cd "$(dirname "$_gcc")/.." && pwd)/m68k-cbm-sysv4/sysroot
	[ -d "$_real/usr/include/sys" ] || { echo "no cross sysroot at $_real" >&2; exit 1; }
	KIT_SYSROOT="$1/sysroot-modelb"; mkdir -p "$KIT_SYSROOT/usr/include/sys"
	for _e in "$_real"/usr/*; do [ "$(basename "$_e")" = include ] || ln -s "$_e" "$KIT_SYSROOT/usr/"; done
	for _e in "$_real"/usr/include/*; do [ "$(basename "$_e")" = sys ] || ln -s "$_e" "$KIT_SYSROOT/usr/include/"; done
	for _e in "$_real"/usr/include/sys/*; do ln -s "$_e" "$KIT_SYSROOT/usr/include/sys/"; done
	for _h in immu param; do
		rm -f "$KIT_SYSROOT/usr/include/sys/$_h.h"
		ln -s "$_real/usr/include/sys/$_h.h" "$KIT_SYSROOT/usr/include/sys/${_h}_stock.h"
		cp "$PORT/include-modelb/sys/$_h.h" "$KIT_SYSROOT/usr/include/sys/"
	done
	_cf=$(echo "$AMIX_KERNEL_CFLAGS" | sed 's/-m68020/-m68040/')
	KIT_CF040="-I$KIT_SYSROOT/usr/include $_cf"
	AMIX_SYSROOT="$KIT_SYSROOT" m68k-cbm-sysv4-gcc $KIT_CF040 -c "$PORT/src/modelb_geom_probe.c" \
		-o "$1/probe.o" || { echo "[FAIL] the Model-B geometry probe does not compile through $KIT_SYSROOT"; exit 1; }
	# the negative check: the same flags without the Model-B set must fail
	if AMIX_SYSROOT="$_real" m68k-cbm-sysv4-gcc $_cf -c "$PORT/src/modelb_geom_probe.c" \
		-o /dev/null 2>/dev/null; then
		echo "[FAIL] the geometry probe compiles against the stock sysroot too: it proves nothing"; exit 1; fi
	rm -f "$1/probe.o"
	echo "[OK] Model-B headers (port/include-modelb) in the compile"
}

# kit_check_040 OBJ: no 2 KB page shift (port/src/check_page_geometry.sh) and
# no 64-bit mul/div form (the 68060 traps them: vector 61)
kit_check_040() {
	OBJDUMP=$OBJDUMP sh "$PORT/src/check_page_geometry.sh" "$1"
	if $OBJDUMP -d "$1" | grep -E "(mul|div)[su]l[.a-z]*\s+[^,]*,%d[0-7]:%d[0-7]"; then
		echo "[FAIL] $(basename "$1") has 64-bit mul/div forms (060 vector 61)"; exit 1; fi
	echo "[OK] $(basename "$1"): no 2 KB page shift, no 64-bit mul/div"
}
