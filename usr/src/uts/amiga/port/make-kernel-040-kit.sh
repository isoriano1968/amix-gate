#!/bin/sh
# make-kernel-040-kit.sh [OUTDIR] -- the kernel-040 kit (package
# developer/amix/kernel-040-kit, installed as
# <prefix>/share/amix-installer/kernel-040): the port's relink chain and its
# own inputs, so the AMIX Installer (or anyone) makes our 68040/68060 kernel
# from a stock AMIX 2.1c kernel with one call:
#
#   <kit>/amix-kernel-040 [--variant generic|z3660] BASE OUT
#
# Ships (MIT, this tree): the relink-040*.sh stages the two variants use,
# build-fpsp040.sh, build-fpsp060.sh, src/ (override sources, byte patchers,
# check_relink_relocs.py, Model-B checks), include-modelb/, tools/ (the
# chain's helpers), LICENSE, NOTICE, THIRD_PARTY_NOTICES/ (Motorola FPSP and
# 060SP, NetBSD FPE notices: their code is extracted at build time from the
# pinned NetBSD 10.1 syssrc.tgz, never vendored here); for the z3660
# variant the driver sources beside it, as in amix-gate (all MIT):
# io/z3660scsi (src/z3660.c, port/patches/), io/z3660net (src/, port/patches/),
# io/zz9000 (zz9000.c/.h), io/paula (port/src: paula.c/.h, patch_audio_paula.py,
# patch_paula_cdevsw.py), each with its LICENSE.
# Never ships: any AMIX/Commodore file (kernel, usr/sys), build/, docs/,
# private/, local/, config.sh, test-tools/.
#
# KIT.info: id=kernel-040, version, variants, base_code_sha256, netbsd pin.
# KIT.sha256: every file, the tree's identity (amix-kernel-040 hashes the same
# tree for the default build id).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/kernel-040-kit}
VERSION=0.1
rm -rf "$OUT"; mkdir -p "$OUT/port" "$OUT/io"
eval "$(grep '^PORT_FILES=' "$HERE/tools/amix-kernel-040")"
(cd "$HERE" && tar cf - --exclude=__pycache__ $PORT_FILES) | (cd "$OUT/port" && tar xf -)
ZZ=zz9000/overlay/usr/sys/amiga/driver/zz9000
(cd "$HERE/../io" && tar cf - z3660scsi/LICENSE z3660scsi/src/z3660.c z3660scsi/port/patches \
	z3660net/LICENSE z3660net/src/z3660eth.c z3660net/src/z3660eth.h z3660net/src/z3660ethuser.h \
	z3660net/port/patches zz9000/LICENSE $ZZ/zz9000.c $ZZ/zz9000.h \
	paula/LICENSE paula/port/src/paula.c paula/port/src/paula.h \
	paula/port/src/patch_audio_paula.py paula/port/src/patch_paula_cdevsw.py) | (cd "$OUT/io" && tar xf -)
cp "$HERE/tools/amix-kernel-040" "$OUT/amix-kernel-040"
chmod 755 "$OUT/amix-kernel-040" "$OUT/port/tools/amix-kernel-040"
. "$HERE/tools/netbsd-pin.sh"
NORM=$(sed -n 's/^BASE_NORM_SHA256=//p' "$HERE/tools/amix-kernel-040")
cat > "$OUT/KIT.info" <<EOF
id=kernel-040
version=$VERSION
line=040
variants=generic z3660
base=AMIX 2.1c kernel (amigaux/amix-system kernels/2.1c/unix, or a 2.1c/2.1p2a /stand/unix)
base_code_sha256=$NORM
netbsd_syssrc=NetBSD 10.1 syssrc.tgz sha256 $NETBSD_SYSSRC_SHA256
entry=amix-kernel-040
EOF
(cd "$OUT" && find . -type f ! -name KIT.sha256 | LC_ALL=C sort | sed 's|^\./||' | \
	xargs sha256sum > KIT.sha256)
echo "[OK] kernel-040 kit $VERSION in $OUT ($(wc -l < "$OUT/KIT.sha256") files)"
