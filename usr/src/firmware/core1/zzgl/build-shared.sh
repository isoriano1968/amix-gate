#!/bin/sh
# build-shared.sh STAGE [TOOLS] -- the 060's side of zzgl as libzzgl.so.1 in
# STAGE/usr/local/lib, with its headers and zzgl.pc.  Mesa 18.3 itself is part
# of the core 1 program; this is all that a program on AMIX links.  A program
# linked before libc060's memcpy gets lib060's move16 copy to the board.
# TOOLS: amix-userland's tools, for amix-got-check.
set -e
STAGE=$1
H=$(cd "$(dirname "$0")" && pwd)
TOOLS=${2:-$H/../../../../../../amix-userland/tools}
C=$H/..
export PATH=$HOME/opt/m68k-amix/bin:$PATH
L=$STAGE/usr/local/lib
I=$STAGE/usr/local/include/zzgl
O=$(mktemp -d)
python3 -I "$H/gen.py" "$H/gen"
F="-O2 -DNDEBUG -m68060 -fPIC -fno-strict-aliasing -fcommon -w -U__linux__ -Ulinux -D__amix__ -DSVR4 \
	-I$H -I$C/src/mesa-18.3.6/include -I$C/compat/amix -I$C/cores/h264bench"
m68k-elf-gcc $F -c "$H/zzgl_enc.c" -o "$O/zzgl_enc.o"
m68k-elf-gcc $F -c "$H/zzgl_amix.c" -o "$O/zzgl_amix.o"
"$TOOLS/amix-got-check" "$O"/*.o
mkdir -p "$L/pkgconfig" "$I"
m68k-elf-gcc -shared -Wl,-h,libzzgl.so.1 "$O"/*.o -o "$L/libzzgl.so.1"
ln -sf libzzgl.so.1 "$L/libzzgl.so"
cp "$H/zzgl_enc.h" "$H/zzgl_platform.h" "$I/"
cat > "$L/pkgconfig/zzgl.pc" <<P
prefix=/usr/local
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: zzgl
Description: GL on the Z3660's core 1, and sound on its HDMI
Version: 1
Libs: -L\${libdir} -lzzgl
Cflags: -I\${includedir}/zzgl
P
rm -rf "$O"
ls -l "$L/libzzgl.so.1"
