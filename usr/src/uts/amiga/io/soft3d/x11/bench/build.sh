#!/bin/sh
# x11/bench/build.sh -- cross-build the P-5 step-0 X clients for Amix:
#   x11perf     from the X11R6.3 tree the Xzz9000 server comes from
#   xbench      Antti's xrtg-amix tools/xbench.c, unchanged
#   xorfill     P-5 suspect S1 (x11/bench/xorfill.c)
#   copyexpose  P-5 suspect S2 (x11/bench/copyexpose.c)
#   xorcopy     P-5 S4/S5 (x11/bench/xorcopy.c)
#   xdrag       an XTEST title-bar drag, for the outline-move frame (x11/bench/xdrag.c)
#   blit-edge   P-5 fix 3: blitter paths against a client-side reference (x11/bench/blit-edge.c)
#   zline       zero-width line rasterisation checksum (x11/bench/zline.c)
#   zz9k_blitbench  per-record blitter timing through /dev/zz9000, no X (x11/bench/zz9k_blitbench.c)
#
# All four link the X11R6.3 Xlib (and x11perf's Xmu/Xt/SM/ICE/Xext) STATICALLY,
# so they do not depend on the X11R4/R5 libraries on the box, and
# libsocket.so/libnsl.so dynamically by bare name, the same way Xzz9000 does.
#
# Inputs (see docs/X-CROSS.md for how the first two were made):
#   XWORK   a COPY of the Xzz9000 cross work dir (src/xc configured by imake,
#           sysroot-mirror/, stub/libsockstub.a, imakecpp.sh).  This script
#           reconfigures its lib/ and programs/x11perf Makefiles for static
#           libraries, so never point it at the dir the metal server was built in.
#   XBENCH  path to xrtg-amix/tools/xbench.c
#   OUT     output dir for the binaries (default: <repo>/build/rt01, gitignored)
# Toolchain: gcc-cross-amix wrapper on PATH (the fixed one: DT_NEEDED by name).
set -e

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
: "${XWORK:?set XWORK to a copy of the Xzz9000 cross work dir}"
: "${XBENCH:?set XBENCH to xrtg-amix/tools/xbench.c}"
OUT=${OUT:-$repo/build/rt01}
CROSS=${CROSS:-$HOME/opt/amix-cross/bin}

PATH=$CROSS:$PATH
AMIX_SYSROOT=$XWORK/sysroot-mirror
IMAKECPP=$XWORK/imakecpp.sh
export PATH AMIX_SYSROOT IMAKECPP
xc=$XWORK/src/xc
mkdir -p "$OUT" "$XWORK/log"

# 1. Static libraries only: gcc 2.7.2.3's -fpic output has local labels
#    (lc%0:) the cross assembler rejects, so the shared-library build fails.
if ! grep -q 'RT-01 client build' "$xc/config/cf/host.def"; then
    cat >> "$xc/config/cf/host.def" <<'EOF'

/* RT-01 client build: static X libraries only (the cross assembler rejects gcc's -fpic labels) */
#undef HasSharedLibraries
#define HasSharedLibraries NO
#undef ForceNormalLib
#define ForceNormalLib YES
EOF
fi

# 2. Host tools the library builds run: makekeys (Xlib's ks_tables.h) and
#    makestrs (Xt's StringDefs).  Both are plain K&R C for the build host.
cc -std=gnu89 -O -w -I"$xc" -I"$xc/exports/include" \
    -o "$XWORK/makekeys-host" "$xc/lib/X11/util/makekeys.c"
"$XWORK/makekeys-host" < "$xc/exports/include/X11/keysymdef.h" \
    > "$xc/lib/X11/ks_tables.h"
cc -std=gnu89 -O -w -I"$xc" -I"$xc/include" \
    -o "$xc/config/util/makestrs" "$xc/config/util/makestrs.c"

# 3. The libraries, in dependency order.
for d in X11 Xext Xtst ICE SM Xt Xmu; do
    (cd "$xc/lib/$d" && make Makefile >/dev/null 2>&1 &&
     make includes >"$XWORK/log/lib-$d-includes.log" 2>&1 &&
     make all >"$XWORK/log/lib-$d.log" 2>&1) ||
        { echo "build.sh: lib/$d failed, see $XWORK/log/lib-$d.log" >&2; exit 1; }
done

XLIBDIR=$xc/exports/lib
SYSLIBS="-L$AMIX_SYSROOT/usr/ccs/lib -lm -lsocket -lnsl -L$XWORK/stub -lsockstub"

# 4. x11perf: objects from its own Makefile, linked by hand (the imake link
#    line still names libXmu.sa, an SVR3 shared-library stub that a static
#    build does not produce).
(cd "$xc/programs/x11perf" && make Makefile >/dev/null 2>&1 &&
 make x11perf.o bitmaps.o do_tests.o do_simple.o do_rects.o do_valgc.o \
      do_lines.o do_segs.o do_dots.o do_windows.o do_movewin.o do_text.o \
      do_blt.o do_arcs.o do_tris.o do_complex.o do_traps.o \
      >"$XWORK/log/x11perf.log" 2>&1)
(cd "$xc/programs/x11perf" &&
 m68k-cbm-sysv4-gcc -O -o "$OUT/x11perf" \
    x11perf.o bitmaps.o do_tests.o do_simple.o do_rects.o do_valgc.o \
    do_lines.o do_segs.o do_dots.o do_windows.o do_movewin.o do_text.o \
    do_blt.o do_arcs.o do_tris.o do_complex.o do_traps.o \
    -L"$XLIBDIR" -lXmu -lXt -lSM -lICE -lXext -lX11 $SYSLIBS)

# 5. The three small clients: Xlib only.  Plain -O like x11perf and Xzz9000, NOT
#    -m68040: that emits 68040-only FPU opcodes (fdmove, fddiv, ...) which the
#    FPU-less LC060 kernel's 68881 emulation does not handle -- xbench died with
#    Illegal Instruction on the rig (2026-09-27).
XINC="-I$xc/exports/include"
m68k-cbm-sysv4-gcc -O $XINC -o "$OUT/xbench" "$XBENCH" \
    -L"$XLIBDIR" -lX11 $SYSLIBS
for p in xorfill copyexpose xorcopy blit-edge zline; do
    m68k-cbm-sysv4-gcc -O $XINC -o "$OUT/$p" "$here/$p.c" \
        -L"$XLIBDIR" -lX11 $SYSLIBS
done
m68k-cbm-sysv4-gcc -O $XINC -o "$OUT/xdrag" "$here/xdrag.c" \
    -L"$XLIBDIR" -lXtst -lXext -lX11 $SYSLIBS
# No X: the driver's own header, libc plus the same system libraries as the X clients.
m68k-cbm-sysv4-gcc -O -I"$repo/overlay/usr/sys/amiga/driver/zz9000" \
    -o "$OUT/zz9k_blitbench" "$here/zz9k_blitbench.c" $SYSLIBS

# 6. Record what was built.
(cd "$OUT" && sha256sum x11perf xbench xorfill copyexpose xdrag xorcopy blit-edge zline zz9k_blitbench > SHA256SUMS)
cat "$OUT/SHA256SUMS"
