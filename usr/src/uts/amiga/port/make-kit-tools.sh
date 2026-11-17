#!/bin/sh
# make-kit-tools.sh [OUTDIR] -- the AMIX Installer's kernel kit tools (package
# developer/amix/kernel-kit-tools, installed as
# <prefix>/share/amix-installer/kit-tools): what the installer needs once per
# relink, whatever drivers are chosen.  OUTDIR defaults to kit-tools/ here
# (gitignored).
#
#   patch_io_init.py        io_init[0] parinit -> amixdrv_init, run once after
#                           every kit's own patch_*.py
#   check_relink_relocs.py  the loader's relocation check of the result
#   amixdrv_init.s.in       the driver chain stub: @INITS@ -> one `jsr <init>`
#                           per chosen driver, graphics before network
#   patch_int2.py           int2_tbl's aenintr entry -> amixdrv_int2, run once
#                           after every kit's own patch_*.py, before
#                           patch_io_init.py, when a chosen kit has intr=;
#                           in the z3660 port kernel (no aenintr entry) the
#                           Z3660 ethernet's z3660eth_p2_next -> amixdrv_int2
#                           and the stub's first call -> its old target
#   amixdrv_int2.s.in       the level-2 interrupt chain stub: aenintr, then
#                           @INTRS@ -> per chosen driver with KIT.info intr=,
#                           in the kits' order:
#                             movel %sp@(4),%sp@- / jsr <intr> / addql &4,%sp
#   KIT.sha256              the five, in this order
#
# The same patch_io_init.py and check_relink_relocs.py are in every driver
# kit, patch_int2.py in every kit with intr= (same bytes, same hashes);
# nothing here is compiled.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/kit-tools}
rm -rf "$OUT"; mkdir -p "$OUT"
cp "$HERE/src/patch_io_init.py" "$HERE/src/check_relink_relocs.py" "$HERE/src/amixdrv_init.s.in" \
	"$HERE/src/patch_int2.py" "$HERE/src/amixdrv_int2.s.in" "$OUT/"
(cd "$OUT" && sha256sum patch_io_init.py check_relink_relocs.py amixdrv_init.s.in \
	patch_int2.py amixdrv_int2.s.in > KIT.sha256)
echo "[OK] kit tools in $OUT"
cat "$OUT/KIT.sha256"
