#!/bin/sh

# Install the self-contained driver subtree into an AMIX root.
# Run this script from the repository root.

ROOT=${1-}
SRC=overlay/usr/sys/amiga/driver/zz9000
DST=${ROOT}/usr/sys/amiga/driver/zz9000

if test ! -f "$SRC/zz9000.c"; then
	 echo "install-source.sh: run from the repository root" >&2
	 exit 1
fi

mkdir -p "$DST/tools" "$DST/init" || exit 1

cp "$SRC/Makefile" "$SRC/zz9000.c" "$SRC/zz9000.h" "$SRC/mkdev.sh" "$DST" || exit 1
cp "$SRC/tools/Makefile" "$SRC/tools/zz9k_test.c" "$SRC/tools/zz9k_reg.c" "$SRC/tools/zz9k_cat.c" "$SRC/tools/zz9k_rw.c" "$SRC/tools/zz9k_maptest.c" "$SRC/tools/zz9k_mode.c" "$SRC/tools/colorls.c" \
	"$SRC/tools/zz9k_console" "$SRC/tools/zz9k_demo" "$SRC/tools/zz9k_colorls_env" "$DST/tools" || exit 1
cp "$SRC/init/Makefile" "$SRC/init/S99zz9000" "$DST/init" || exit 1

chmod 755 "$DST/mkdev.sh" "$DST/tools/zz9k_console" "$DST/tools/zz9k_demo" \
	"$DST/tools/zz9k_colorls_env" "$DST/init/S99zz9000"

echo "Installed ZZ9000 sources in $DST"
echo "Complete the kernel integration described in INSTALL.md."
