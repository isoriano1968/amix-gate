#!/bin/sh
# libgl-amix.sh STAGE -- libGL.so.18 from xbuild.py amix-glx: Mesa 18 with GLX on
# plain X.  amix_so.c's two ends must be first and last of the link, around all
# of .init_array.  STAGE has the shared X libraries.
set -e
export PATH=$HOME/opt/m68k-amix/bin:$PATH
HERE=$(cd "$(dirname "$0")" && pwd)
C=$HERE/../compat/amix
T=$HERE/../../../../../../amix-userland/tools
X=$1/usr/x11r6/lib
cd "$HERE/amix-glx"
m68k-elf-gcc -O2 -m68060 -fPIC -c "$C/amix_so.c" -o amix_so.o
m68k-elf-gcc -m68060 -fPIC -DAMIX_SO_ENDS=1 -c "$C/amix_so.c" -o so_first.o
m68k-elf-gcc -m68060 -fPIC -DAMIX_SO_ENDS=2 -c "$C/amix_so.c" -o so_last.o
m68k-elf-gcc -O2 -m68060 -fPIC -w -U__INT32_TYPE__ -D__INT32_TYPE__=int -U__UINT32_TYPE__ \
	-D__UINT32_TYPE__='unsigned int' -D__amix__ -DSVR4 -I"$C" -include "$C/amixgl_compat.h" \
	-c "$C/amixgl_compat.c" -o amixgl_compat.o
# zz3660's way to core 1: the ring of zzgl
Z=$HERE/../zzgl
M=$HERE/../src/mesa-18.3.6
F="-O2 -m68060 -fPIC -w -D__amix__ -DSVR4 -I$Z -I$M/include -I$M/src/gallium/drivers/zz3660 -I$HERE/../compat/amix -I$HERE/../cores/h264bench -I$1/usr/x11r6/include"
m68k-elf-gcc $F -c "$Z/zzgl_enc.c" -o zzgl_enc.o
m68k-elf-gcc $F -c "$Z/zzgl_amix.c" -o zzgl_amix.o
m68k-elf-gcc $F -c "$M/src/gallium/drivers/zz3660/zz_board.c" -o zz_board.o
m68k-elf-gcc -shared -Wl,-h,libGL.so.18 so_first.o obj/*.o amix_so.o amixgl_compat.o \
	zz_board.o zzgl_enc.o zzgl_amix.o -L"$X" \
	-lXext -lX11 -lstdc++ -lsupc++ -lm so_last.o -o libGL.so.18
m68k-elf-strip libGL.so.18
"$T/amix-fix-data-relocs" libGL.so.18
ln -sf libGL.so.18 libGL.so
ls -l libGL.so.18
