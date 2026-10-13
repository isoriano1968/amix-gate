#!/bin/sh
# band-test.sh <zz9000draw.c> -- host test of the Xzz9000 blitter queue (RT-08): takes the
# run helpers, cpuFill/cpuCopy, blitFlush/blitRecord/blitQueue, blitFill and copyVRAMBox
# out of a build tree's zz9000draw.c, runs them against a simulated framebuffer and a
# fake ZZ9KIOC_BLITS (records executed one by one, or the Nth flush refused), and compares
# every fill and copy with a direct reference.  6930 cases: bands of 0/64/500 px, ten copy
# directions, refusal at flush 1-4 or never.  Host longs are mapped to 32 bits as on m68k.
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=${1:?usage: band-test.sh path/to/zz9000draw.c}
tmp=${TMPDIR:-/tmp}/band-test.$$
mkdir -p "$tmp"
python3 - "$src" "$tmp/band-funcs.c" <<'PY'
import sys
s = open(sys.argv[1]).read()
body = s[s.index('#define RUN_ODD(p)'):s.index('/* ZZ9000GetImage')]
a = body.index('/*\n** Pixel base and stride for a drawable'); b = body.index('/*\n** cpuCopy')
open(sys.argv[2], 'w').write((body[:a] + body[b:]).replace('unsigned long', 'unsigned int'))
PY
cp "$here/band-test.c" "$tmp/"
cc -O -w -o "$tmp/band-test" "$tmp/band-test.c"
"$tmp/band-test"; rc=$?
rm -rf "$tmp"
exit $rc
