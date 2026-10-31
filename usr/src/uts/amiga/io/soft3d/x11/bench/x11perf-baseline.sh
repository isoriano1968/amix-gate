#!/bin/sh
# x11perf-baseline.sh -- record an Xzz9000 performance baseline on Amix.
#
# Adapted from Antti Sokero's xrtg-amix x11perf-baseline.sh (MIT licence,
# Copyright (c) 2025-2026 Antti Sokero).  The TEST LIST, SECS and REPS are
# kept VERBATIM so a Z3660 run and an Xrtg/VA2000 run compare line by line
# with x11perfcomp.  What differs: the x11perf binary is ours (cross-built
# from X11R6.3, see RUN.md) and found beside this script, and the header
# takes the server's lines from the Xzz9000 log instead of the Xrtg one.
#
# Usage:
#   sh x11perf-baseline.sh [outfile] [label]
#
#   outfile   where to write the report   (default /tmp/xzz9000-perf.txt)
#   label     free-text note stored in the header, e.g. "p5-0b z3660 060 1280x800"
#
# Run as the user who started the server, with DISPLAY=:0 (not host:0: the
# file is meant to be shareable and DISPLAY is recorded verbatim).  Do not
# touch the mouse or keyboard while it runs.

OUT=${1:-/tmp/xzz9000-perf.txt}
LABEL=${2:-unlabelled}
DISPLAY=${DISPLAY:-:0}
export DISPLAY

# Verbatim from xrtg-amix: 3 s per repetition, 3 repetitions.  Read the
# spread x11perf reports, not just the average -- one number from this
# class of machine means very little.
SECS=3
REPS=3

TESTS="-dot \
-rect1 -rect10 -rect100 -rect500 \
-line10 -line100 \
-circle10 -circle100 \
-scroll10 -scroll100 -scroll500 \
-copywinwin10 -copywinwin100 -copywinwin500 \
-copypixwin100 -copywinpix100 -copypixpix100 \
-putimage10 -putimage100 \
-getimage10 -getimage100 \
-text -ftext"

here=`dirname $0`
X11PERF=${X11PERF:-$here/x11perf}
SERVERLOG=${RTG_SERVERLOG:-/tmp/Xzz9000.log}
PATH=/usr/X/bin:/usr/bin/X11:/usr/bin:/bin
export PATH

if [ ! -x "$X11PERF" ]; then
    echo "ERROR: $X11PERF not found or not executable (set X11PERF=)." >&2
    exit 1
fi

echo "Writing $OUT (label: $LABEL)"
echo "This takes a while -- do not touch the mouse or keyboard."

echo "# Xzz9000 performance baseline"                    >  "$OUT"
echo "# label:   $LABEL"                                 >> "$OUT"
echo "# date:    `date`"                                 >> "$OUT"
# uname -srvm, not -a: -a includes the node name.
echo "# system:  `uname -srvm`"                          >> "$OUT"
echo "# display: $DISPLAY"                               >> "$OUT"
echo "# timing:  $SECS s x $REPS reps per test"          >> "$OUT"
echo "# x11perf: $X11PERF"                               >> "$OUT"

# The server's own lines identify the configuration: the mode it set and
# the framebuffer geometry.
if [ -f "$SERVERLOG" ]; then
    grep "ZZ9000" "$SERVERLOG" | sed 's/^/# /'           >> "$OUT"
fi

# One grep per pattern: this shell's grep has no alternation.
xdpyinfo > /tmp/xzz-perf-dpy.$$ 2>/dev/null
if [ -s /tmp/xzz-perf-dpy.$$ ]; then
    grep "dimensions"    /tmp/xzz-perf-dpy.$$ | sed 's/^ */# /' >> "$OUT"
    grep "depth of root" /tmp/xzz-perf-dpy.$$ | sed 's/^ */# /' >> "$OUT"
    grep "MIT-SHM"       /tmp/xzz-perf-dpy.$$ | sed 's/^ */# ext: /' >> "$OUT"
fi
rm -f /tmp/xzz-perf-dpy.$$

echo "#" >> "$OUT"

for t in $TESTS
do
    echo "  $t"
    $X11PERF -time $SECS -repeat $REPS $t >> "$OUT" 2>&1
    if [ $? -ne 0 ]; then
        echo "# NOTE: $t failed or is not supported by this x11perf" >> "$OUT"
    fi
done

echo "" >> "$OUT"
echo "Done.  Results in $OUT"
