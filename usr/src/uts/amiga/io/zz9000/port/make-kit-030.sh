#!/bin/sh
# make-kit-030.sh [OUTDIR] -- the AMIX Installer's zz9000 kit for STOCK 68030
# kernels: /dev/zz9000 (major 49) for the MNT ZZ9000 on Zorro II, the
# driver's original target (A3000/A2000 with stock AMIX 2.1/2.1p2a).  OUTDIR
# defaults to kit/ beside this script (gitignored).
#
#   zz9000_030.o            overlay/usr/sys/amiga/driver/zz9000/zz9000.c,
#                           built for the stock 2 KB-page kernel
#   zz9000_pad.o            pads the kernel's .data back to a multiple of 4
#   patch_zz9000_cdevsw.py  cdevsw[49] d_open..d_mmap (by symbol, asserting nodev)
#   patch_io_init.py        io_init[0] parinit -> the installer's driver chain
#                           amixdrv_init, which calls zz9000init (port/src)
#   check_relink_relocs.py  the loader's relocation check
#   KIT.info, KIT.sha256
#
# Compile switches: none of the target or port switches.  No ZZ9K_TARGET_*:
# the ZZ9000 rows only (Zorro II product 3 attached; the Zorro III product 4
# is named and refused, its window is outside the stock kernel's reach).  No
# ZZ9K_KVMAP, ZZ9K_FB_NC, ZZ9K_MONSW, ZZ9K_MAP_BOARD: they need the 68040/
# 68060 port kernel's dev_kvmap, hat_cm_fb_add and monsw.  ZZ9K_NO_BUILTINFONT:
# the stock kernel has no builtinfont[] (the driver's Makefile links AMIX's
# console/sunfont.o, Commodore's, which a kit cannot carry); the optional RTG
# console (ZZ9KIOC console ioctls, S99zz9000) then draws with the driver's own
# 5x7 glyphs.  It swaps the stock conputc only when asked by ioctl, never at
# boot; the c1.c hook (integration/c1-console.txt) is not in a stock kernel and
# nothing calls zz9000consoleputc there.  The vt040.c entry points
# (zz9000vtinfo/vtpan) are only called by the port kernel's VT switcher.
# zz9000init probes once at boot and is silent without a board.
#
# Kernel code is compiled with the gcc 2.7.2.3 cross compiler only.  Needs
# AMIX_ROOT (an AMIX 2.1c tree with usr/include and usr/sys, including
# usr/sys/amiga/console: github.com/amigaux/amix-system) and
# m68k-cbm-sysv4-gcc (developer/amix/gcc-27) on PATH.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
PORT=$(cd "$HERE/../../../port" && pwd)
SRC=$(cd "$HERE/../overlay/usr/sys/amiga/driver/zz9000" && pwd)
. "$PORT/tools/kit-lib.sh"
kit_env
OUT=${1:-$HERE/kit}
VERSION=0.1
[ -f "$AMIX_ROOT/usr/sys/amiga/console/screen.h" ] || {
	echo "AMIX_ROOT has no usr/sys/amiga/console/screen.h" >&2; exit 1; }

# zz9000.c includes ../../console/{screen,console}.h: the AMIX tree's layout
B="$HERE/build/zz9000kit"; rm -rf "$B"; mkdir -p "$B/amiga/driver/zz9000"
ln -s "$AMIX_ROOT/usr/sys/amiga/console" "$B/amiga/console"
cp "$SRC/zz9000.c" "$SRC/zz9000.h" "$B/amiga/driver/zz9000/"
CF="$AMIX_KERNEL_CFLAGS -Dsigned=__signed__ -DZZ9K_NO_BUILTINFONT"
(cd "$B/amiga/driver/zz9000" && m68k-cbm-sysv4-gcc $CF -I. -c zz9000.c -o "$B/zz9000_030.o")
kit_check_030 "$B/zz9000_030.o"
U=$($NM "$B/zz9000_030.o" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
[ "$U" = "autocon bootinfo conputc copyin copyout printf uiomove " ] || {
	echo "[FAIL] zz9000_030.o imports: $U"; exit 1; }
echo "[OK] imports only what the stock kernel has: $U"
kit_pad "$B/zz9000_030.o" "$B/zz9000_pad.o"

rm -rf "$OUT"; mkdir -p "$OUT"
cp "$B/zz9000_030.o" "$B/zz9000_pad.o" "$HERE/src/patch_zz9000_cdevsw.py" \
	"$KIT_IOINIT" "$KIT_RELOCS" "$OUT/"
kit_info "$OUT" id=zz9000 version=$VERSION major=49 node=/dev/zz9000 line=030 init=zz9000init \
	"entries=zz9000init zz9000open zz9000close zz9000read zz9000write zz9000ioctl zz9000mmap" \
	streams=no
kit_sums "$OUT" zz9000_030.o zz9000_pad.o patch_zz9000_cdevsw.py patch_io_init.py \
	check_relink_relocs.py KIT.info
rm -rf "$B"
echo "[OK] zz9000 kit $VERSION in $OUT"
cat "$OUT/KIT.info" "$OUT/KIT.sha256"
