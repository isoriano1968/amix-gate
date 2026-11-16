# m68k-binutils.sh -- sourced: the modern m68k ELF binutils for the checks.
#
# The checks only read objects (objdump, readelf, nm) and set one section
# alignment (objcopy); they never generate kernel code.  Prefer the m68k-elf
# tools that developer/amix/gcc-14 ships (also on OpenIndiana), else Debian's
# m68k-linux-gnu ones.  Kernel code itself is always compiled and linked with
# the gcc 2.7.2.3 toolchain (m68k-cbm-sysv4-gcc, -ld).
for _p in m68k-elf m68k-linux-gnu; do
	if command -v $_p-objdump >/dev/null 2>&1 && command -v $_p-readelf >/dev/null 2>&1 &&
	   command -v $_p-objcopy >/dev/null 2>&1 && command -v $_p-nm >/dev/null 2>&1; then
		OBJDUMP=$_p-objdump; READELF=$_p-readelf; OBJCOPY=$_p-objcopy; NM=$_p-nm
		break
	fi
done
[ -n "$OBJDUMP" ] || {
	echo "ERROR: no m68k ELF binutils (m68k-elf-* from developer/amix/gcc-14, or m68k-linux-gnu-*)" >&2
	exit 1; }
$OBJCOPY --help 2>&1 | grep -q set-section-alignment || {
	echo "ERROR: $OBJCOPY has no --set-section-alignment (binutils too old)" >&2; exit 1; }
