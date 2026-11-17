#!/bin/sh
# make-kit-040.sh [OUTDIR] -- the AMIX Installer's z3660eth kit for the
# 68040/68060 PORT kernels (Model B, 4 KB pages): the Z3660's ethernet as the
# STREAMS DLPI driver z3660ethinfo, /dev/zen0 (major 51).  The Z3660 is a
# 68060 accelerator, so there is no 68030 kit.  OUTDIR defaults to kit-040/
# beside this script (gitignored).
#
#   z3660eth_040.o          ../src (Jussi Alanärä's z3660eth, pristine) with
#                           patches/ applied in order -- 0001..0005 are the
#                           driver of the A3000 + Z3660 kernel (eth 1.0.0-7,
#                           dev_kvmap mapping, INT6 code), 0006 makes it
#                           silent without a board -- compiled -m68040 against
#                           the Model-B headers with -DZ3660_KVMAP, and
#                           dev_kvmap/dev_kvunmap named z3660eth_kvmap/
#                           z3660eth_kvunmap (below)
#   z3660eth_kvmap.o        port/src/devkvmap040.s with the same two names
#   z3660eth_pad.o          pads the kernel's .data back to a multiple of 4
#   patch_z3660eth_cdevsw.py  cdevsw[51].d_str -> z3660ethinfo (STREAMS: the
#                           slot's d_xhalt relocation moved to d_str)
#   patch_io_init.py        (unused by this kit: no init; for the chain of the
#                           kits linked with it, as in every kit)
#   check_relink_relocs.py  the loader's relocation check
#   LICENSE.z3660net        the driver's MIT licence
#   KIT.info (line=040), KIT.sha256
#
# Lazy open, no init and no interrupt, as in the A3000 + Z3660 kernel: the
# driver looks for the board at the first open of /dev/zen0 (AutoConfig
# 0x144B0001; on AGA only also the fixed window 0x10000000, behind a bprobe),
# and until then touches nothing.  Receive is polled from a clock callout;
# the INT6 receive of that kernel (-DZ3660ETH_STATIC_INTR, which retargets
# p6int and int2_tbl slot 4) is not in the kit: it needs the shared level-2
# chain (port/src/patch_int2.py, KIT.info intr=) and a level-6 chain, and is
# opt-in on the box anyway (zenintr).
#
# dev_kvmap: a Z3660 at or above VSECT1 (autoconfig_rtg YES, 0x50000000 on an
# A3000) is mapped noncacheable through the port's dev_kvmap, a stateless
# wrapper over sptalloc.  The va2000 kit-040 carries its own dev_kvmap and
# kernels like m6-msp have it already, so this kit carries a privately named
# copy: any combination links once.
#
# Kernel code is compiled with the gcc 2.7.2.3 cross compiler only.  Needs
# AMIX_ROOT (or a port config.sh) and m68k-cbm-sysv4-gcc on PATH, as the
# other kits.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../../../port" && pwd)
SRC=$(cd "$HERE/../src" && pwd)
. "$PORT/tools/kit-lib.sh"
kit_env
OUT=${1:-$HERE/kit-040}
VERSION=0.1

B="$HERE/build/z3660ethkit040"; rm -rf "$B"; mkdir -p "$B/src"
cp "$SRC/z3660eth.c" "$SRC/z3660eth.h" "$SRC/z3660ethuser.h" "$B/src/"
for p in "$HERE"/patches/*.patch; do
	(cd "$B" && patch -p1 -s --no-backup-if-mismatch < "$p") || {
		echo "[FAIL] $(basename "$p") does not apply"; exit 1; }
	echo "[OK] $(basename "$p")"
done
kit_modelb_sysroot "$B"
(cd "$B/src" && AMIX_SYSROOT="$KIT_SYSROOT" m68k-cbm-sysv4-gcc $KIT_CF040 -DZ3660_KVMAP \
	-Ddev_kvmap=z3660eth_kvmap -Ddev_kvunmap=z3660eth_kvunmap -I. \
	-c z3660eth.c -o "$B/z3660eth_040.o")
kit_check_040 "$B/z3660eth_040.o"
U=$($NM "$B/z3660eth_040.o" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
WANT="OTHERQ RD WR allocb autocon bcmp bcopy bprobe bzero canput cmn_err cputype flushq freemsg getmajor getminor getq ifstats lbolt makedevice memcpy panicstr putbq putnext putq qreply sptfree timeout unlinkb untimeout z3660eth_kvmap z3660eth_kvunmap "
[ "$U" = "$WANT" ] || { echo "[FAIL] z3660eth_040.o imports: $U"; exit 1; }
echo "[OK] imports: $U(-DZ3660_KVMAP took)"
m68k-cbm-sysv4-gcc -m68040 -c "$PORT/src/devkvmap040.s" -o "$B/devkvmap040.o"
$OBJCOPY --redefine-sym dev_kvmap=z3660eth_kvmap --redefine-sym dev_kvunmap=z3660eth_kvunmap \
	"$B/devkvmap040.o" "$B/z3660eth_kvmap.o"
D=$($NM "$B/z3660eth_kvmap.o" | awk '$2=="T"{print $3}' | LC_ALL=C sort | tr '\n' ' ')
[ "$D" = "z3660eth_kvmap z3660eth_kvunmap " ] || { echo "[FAIL] z3660eth_kvmap.o defines: $D"; exit 1; }
kit_pad "$B/z3660eth_040.o" "$B/z3660eth_kvmap.o" "$B/z3660eth_pad.o"

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/z3660eth_040.o" "$B/z3660eth_kvmap.o" "$B/z3660eth_pad.o" "$HERE/src/patch_z3660eth_cdevsw.py" \
	"$KIT_IOINIT" "$KIT_RELOCS" "$OUT/"
cp "$HERE/../LICENSE" "$OUT/LICENSE.z3660net"
kit_info "$OUT" id=z3660eth version=$VERSION major=51 node=/dev/zen0 line=040 init= intr= \
	"entries=z3660ethinfo z3660eth_kvmap z3660eth_kvunmap" streams=yes
kit_sums "$OUT" z3660eth_040.o z3660eth_kvmap.o z3660eth_pad.o patch_z3660eth_cdevsw.py \
	patch_io_init.py check_relink_relocs.py LICENSE.z3660net KIT.info
rm -rf "$B"
echo "[OK] z3660eth kit $VERSION (040) in $OUT"
cat "$OUT/KIT.info" "$OUT/KIT.sha256"
