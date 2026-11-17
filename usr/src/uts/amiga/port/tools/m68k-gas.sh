# m68k-gas.sh -- sourced: the modern GNU m68k toolchain for the FPSP/ISP stages, and
# m68k_asm, which assembles so that m68k-elf and m68k-linux-gnu give the same object.
#
#     HERE=<the port directory>; . "$HERE/tools/m68k-gas.sh"
#     m68k_asm OBJ SRC [gcc options...]
#
# Sets M68K_GCC M68K_LD M68K_NM M68K_READELF M68K_OBJCOPY M68K_SIZE: the m68k-elf tools
# (developer/amix/gcc-14, /opt/m68k-amix/bin: the only ones on OpenIndiana and in the
# linux-amix packages) when they are on PATH, else Debian's m68k-linux-gnu ones.  Both are
# GNU binutils 2.4x with the same elf32-m68k code; ld -r, nm, objcopy and readelf give the
# same bytes.  The assembler does not, by configuration (gas/config/tc-m68k.h):
#
#   #define EXTERN_FORCE_RELOC \
#     ((strcmp (TARGET_OS, "elf") != 0) && (strcmp (TARGET_OS, "uclinux") != 0))
#
# m68k-linux-gnu gas treats a GLOBAL symbol as one a shared library may override: a branch
# or reference to it, even in the same section of the same file, keeps its relocation
# against the symbol, is never relaxed to a shorter form, and never becomes section+offset.
# m68k-elf gas (an embedded target: no shared libraries) resolves such references at
# assembly time: fewer relocations, shorter branches, PC-relative lea (the 68040 FPSP
# came out 4 bytes smaller, .text 0x994e for 0x9952; fpsp_glue040.o and fpsp060_pkg.o
# relocate against .data/.text instead of their symbols).  No gas option selects either
# policy; TARGET_OS is fixed when gas is built.
#
# Both assemblers apply the extern policy to WEAK symbols (relaxable_symbol, S_FORCE_RELOC
# strict).  So m68k_asm assembles once, lists the symbols the object defines globally,
# assembles again with those declared .weak (.weak overrides .globl in gas, whatever the
# order), and gives the symbols their global binding back
# (tools/elf-global.py, only the listed ones, in the symbol table entry, nothing else).
# The result is the m68k-linux-gnu object, byte for byte, with either assembler: the
# kernel this port has always built.  With m68k-linux-gnu the second pass changes nothing
# (asserted by the 2026-11-17 comparison: every object and both kernel variants identical).

M68K_GCC= M68K_LD= M68K_NM= M68K_READELF= M68K_OBJCOPY= M68K_SIZE=
for _p in m68k-elf m68k-linux-gnu; do
	if command -v $_p-gcc >/dev/null 2>&1 && command -v $_p-ld >/dev/null 2>&1 &&
	   command -v $_p-nm >/dev/null 2>&1 && command -v $_p-readelf >/dev/null 2>&1 &&
	   command -v $_p-objcopy >/dev/null 2>&1 && command -v $_p-size >/dev/null 2>&1; then
		M68K_GCC=$_p-gcc M68K_LD=$_p-ld M68K_NM=$_p-nm M68K_READELF=$_p-readelf
		M68K_OBJCOPY=$_p-objcopy M68K_SIZE=$_p-size
		break
	fi
done
[ -n "$M68K_GCC" ] || {
	echo "ERROR: no m68k ELF gcc + binutils (m68k-elf-* from developer/amix/gcc-14, or m68k-linux-gnu-*)" >&2
	exit 1; }
# the caller's $HERE is the port directory (every stage script sets it)
[ -f "${HERE:?}/tools/elf-global.py" ] || { echo "ERROR: $HERE/tools/elf-global.py not found" >&2; exit 1; }

# m68k_asm OBJ SRC [gcc options...]: $M68K_GCC -x assembler-with-cpp OPTIONS -c SRC -o OBJ,
# with the extern policy above for the symbols SRC defines globally.  The .weak list goes
# AFTER the source (a wrapper that #includes SRC, then the list): gas numbers the symbols in
# the order it first meets them, and that order is the symbol table's.
m68k_asm() {
	_ma_o=$1 _ma_src=$2; shift 2
	case $_ma_src in /*) ;; *) _ma_src=$(pwd)/$_ma_src ;; esac
	$M68K_GCC -x assembler-with-cpp "$@" -c "$_ma_src" -o "$_ma_o" || return 1
	# defined, global, not weak, not common: what an extern-policy gas keeps relocations for
	_ma_s=$($M68K_NM -g --defined-only "$_ma_o" | awk 'NF == 3 && $2 ~ /^[ABDGRST]$/ {print $3}' | LC_ALL=C sort -u)
	[ -n "$_ma_s" ] || return 0
	{ printf '#include "%s"\n' "$_ma_src"
	  for _ma_n in $_ma_s; do printf '\t.weak\t%s\n' "$_ma_n"; done; } > "$_ma_o.weak.S"
	$M68K_GCC -x assembler-with-cpp "$@" -c "$_ma_o.weak.S" -o "$_ma_o" || return 1
	rm -f "$_ma_o.weak.S"
	python3 -I "$HERE/tools/elf-global.py" "$_ma_o" $_ma_s
}
