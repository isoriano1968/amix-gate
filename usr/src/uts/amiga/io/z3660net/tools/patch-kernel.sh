#!/bin/sh
# On-box integration of z3660eth into master.d/kernel.c (run on the AMIX box).
# Adds the streamtab extern + claims cdevsw slot 51.  Uses ed (portable on SVR4)
# since FTP pull-back of the 14 KB kernel.c is unreliable.  Idempotent.
#
# Usage: sh patch-kernel.sh [path-to-kernel.c]      (default /usr/sys/master.d/kernel.c;
# the argument exists so tools/patch-selftest.sh can drive it over fixtures, benchless.)
#
# PORTABILITY -- this runs under the box's pre-POSIX Bourne /bin/sh with SVR4 ed:
# backticks (never the $-paren form), no [[, no local, no arrays, no $-paren-paren
# arithmetic, no printf(1), no grep -q/-E, no sed -i/-E.  tools/patch-selftest.sh
# lints for every one of those.
#
# TWO INDEPENDENT ARMS, each separately guarded, each separately post-verified:
#   A  the streamtab extern, inserted after the stock aen one
#   B  the cdevsw[] row for major 51
# Splitting them is the point: a miss on one must never be masked by the other, and
# either arm alone is a repairable state rather than a permanent one.
#
# EXIT STATUS -- the four outcomes are distinguishable by token and by rc:
#   0  KERNEL.C ALREADY PATCHED    both arms already present; nothing written
#   0  KERNEL.C PATCHED:           every arm that was needed landed AND was verified
#   4  KERNEL.C REFUSED (<v>):     pre-flight said no; the file was NOT touched
#   5  KERNEL.C HALF-PATCHED:      an arm did not land; restore the backup, do not relink
# Verdict tokens are kerntools tools/cdevsw-gate.sh's vocabulary on purpose --
# FREE / OURS / TAKEN / HALF / MISSING / MOVED -- so both sides refuse in one language.
#
# WHY THE PRE-FLIGHT IS ROW-SCOPED.  The old guard was `grep z3660eth $K`: name-wide.
# On any tree integrated BEFORE the 2026-07-30 48->51 renumber the name is there --
# at the old row -- so the guard reported ALREADY PATCHED, slot 51 was never claimed,
# and renumbered /dev/zen0 nodes opened an empty major.  A silent no-ethernet skew that
# every tool in the chain called success.  (workspace half-patch audit 2026-07-31, F1.)
#
# WHY ED'S EXIT STATUS IS NOT THE CHECK.  On an address-search miss SVR4 ed prints a
# bare `?`, goes on reading the REST of the here-document as commands, and still writes
# the file; rc 0 either way.  Measured on both arms.  Post-verifying the RESULT in the
# file is the only mechanism that holds.  (audit F5.)
#
# THE ROW TEXT BELOW IS LOAD-BEARING ACROSS REPOS.  kerntools
# tools/cdevsw-gate-selftest.sh lifts the replacement row out of THIS file with
#   grep -E "^ND,.*&z3660ethinfo,.*/\*51=z3660eth\*/"
# and fails if it drifts.  Keep it exactly one line, at column zero, byte-unchanged.

K=${1:-/usr/sys/master.d/kernel.c}

if test ! -f "$K" || test ! -r "$K"; then
	echo "KERNEL.C REFUSED (NOKERNELC): $K is missing or unreadable." >&2
	echo "  Nothing was written." >&2
	exit 4
fi

# ---------------------------------------------------------------- pre-flight ----
# `grep notty` selects the cdevsw table: every cdevsw row carries the d_ttys column on
# the same physical line as its /*<major>*/ tag and no bdevsw row does -- and the two
# tables share a tag space.  Same discriminator as kerntools tools/cdevsw-gate.sh.
ROW51=`grep notty "$K" | grep '/\*51[*=]'`
NROW51=`grep notty "$K" | grep -c '/\*51[*=]'`
STALE=`grep notty "$K" | grep '&z3660ethinfo' | grep -v '/\*51[*=]'`

HAVEEXT=no
grep 'extern struct streamtab z3660ethinfo;' "$K" >/dev/null 2>&1 && HAVEEXT=yes

VERDICT=TAKEN
if test -n "$STALE"; then
	VERDICT=MOVED
elif test "$NROW51" -eq 0; then
	VERDICT=MISSING
elif test "$NROW51" -gt 1; then
	VERDICT=AMBIGUOUS
else
	case "$ROW51" in
	*"&z3660ethinfo"*)			VERDICT=OURS ;;
	*"notty,nostr,nullflag,"*"/*51*/")	VERDICT=FREE ;;
	*)					VERDICT=TAKEN ;;
	esac
fi

if test "$VERDICT" = MOVED; then
	cat >&2 <<EOF
KERNEL.C REFUSED (MOVED): z3660eth already claims a cdevsw row that is NOT major 51, in
  $K
EOF
	echo "$STALE" | sed 's/^/    /' >&2
	cat >&2 <<EOF
  That is a tree integrated BEFORE the 2026-07-30 48->51 renumber.  The old name-wide
  guard reported ALREADY PATCHED here and claimed nothing, while renumbered /dev/zen0
  nodes open major 51 -- silent no-ethernet.  Claiming 51 now without vacating the old
  row would leave TWO live z3660eth rows instead.  MIGRATE FIRST, once:
    1. restore the row above to the stock free filler -- d_str back to nostr, and its
       tag back to the bare form with no "=z3660eth".
    2. leave the extern alone; major 51 still needs it.
    3. re-run this script: it will then see a free /*51*/ row and claim it.
  See BUILD.md step 2 and the workspace half-patch audit F1.  Nothing was written.
EOF
	exit 4
fi

if test "$VERDICT" = MISSING; then
	cat >&2 <<EOF
KERNEL.C REFUSED (MISSING): no cdevsw row in
  $K
  carries a /*51*/ or /*51=...*/ tag.  The slot was retagged -- a claim can read
  "/* 51 jck MAX-125 */" with no "=" -- or the table moved.  ed's address search would
  not match it either: the change would fail while ed wrote the file anyway.
  Nothing was written.
EOF
	exit 4
fi

if test "$VERDICT" = AMBIGUOUS; then
	cat >&2 <<EOF
KERNEL.C REFUSED (AMBIGUOUS): $NROW51 cdevsw rows carry a /*51*/ tag in
  $K
EOF
	echo "$ROW51" | sed 's/^/    /' >&2
	cat >&2 <<EOF
  ed's change command would rewrite only the first one, leaving the others behind.
  Resolve the duplicate by hand (they are usually #ifdef'd variants) and re-run.
  Nothing was written.
EOF
	exit 4
fi

if test "$VERDICT" = TAKEN; then
	cat >&2 <<EOF
KERNEL.C REFUSED (TAKEN): cdevsw major 51 is not the free nostr row in
  $K
EOF
	echo "$ROW51" | sed 's/^/    /' >&2
	cat >&2 <<EOF
  Something else claims it.  Do not overwrite a claim: pick another free major from
  the 51-67 / 69 set -- grimoire-amix/docs/reference/major-number-registry.md, the
  Major-Number Registry -- and renumber, or clear the row deliberately by hand.
  Nothing was written.
EOF
	exit 4
fi

# VERDICT is FREE or OURS from here on: the only two states in which writing is safe.
NEEDROW=no
test "$VERDICT" = FREE && NEEDROW=yes
NEEDEXT=no
test "$HAVEEXT" = no && NEEDEXT=yes

if test "$NEEDROW" = no && test "$NEEDEXT" = no; then
	echo "KERNEL.C ALREADY PATCHED"
	grep -n z3660eth "$K"
	exit 0
fi

# Arm A's anchor is checked with the SAME unanchored pattern ed searches for, so the
# pre-flight predicts ed exactly.  Exactly one occurrence, or we do not start.
if test "$NEEDEXT" = yes; then
	NANCHOR=`grep -c 'extern struct streamtab aeninfo;' "$K"`
	if test "$NANCHOR" -ne 1; then
		cat >&2 <<EOF
KERNEL.C REFUSED (NOANCHOR): the stock line
    extern struct streamtab aeninfo;
  occurs $NANCHOR times (need exactly 1) in
  $K
  Arm A inserts our extern immediately after it.  On a miss ed reads our extern line
  as an ed COMMAND and still writes the file -- the measured half-patch (audit F5).
  Add
    extern struct streamtab z3660ethinfo;
  by hand beside the other streamtab externs, then re-run.  Nothing was written.
EOF
		exit 4
	fi
fi

# ------------------------------------------------------------------- write ------
# $K.bnkorig is the state as this run found it (an earlier run's copy is replaced).
cp "$K" "$K.bnkorig"
chmod 644 "$K"

# Arm A -- the streamtab extern.  Its own ed invocation: a miss here must not reach arm B.
if test "$NEEDEXT" = yes; then
ed - "$K" <<'EOF'
/extern struct streamtab aeninfo;/a
extern struct streamtab z3660ethinfo;
.
w
q
EOF
fi

# Arm B -- the cdevsw row.  The address is row-scoped too (free-filler columns AND the
# tag), so ed can only ever land on the row the pre-flight classified as FREE.
if test "$NEEDROW" = yes; then
ed - "$K" <<'EOF'
/notty,nostr,nullflag,.*\/\*51\*\//c
ND,ND,ND,ND,ND,ND,ND,ND,ND,ND,notty,&z3660ethinfo,nullflag,		/*51=z3660eth*/
.
w
q
EOF
fi

# ------------------------------------------------------------- post-verify -----
GOTEXT=no
grep 'extern struct streamtab z3660ethinfo;' "$K" >/dev/null 2>&1 && GOTEXT=yes
GOTROW=no
grep notty "$K" | grep '/\*51[*=]' | grep '&z3660ethinfo' >/dev/null 2>&1 && GOTROW=yes

if test "$GOTEXT" = yes && test "$GOTROW" = yes; then
	echo "KERNEL.C PATCHED:"
	grep -n z3660eth "$K"
	exit 0
fi

echo "KERNEL.C HALF-PATCHED: ed did not land every arm." >&2
if test "$GOTEXT" = no; then
	echo "  ARM A FAILED: 'extern struct streamtab z3660ethinfo;' is not in $K." >&2
fi
if test "$GOTROW" = no; then
	echo "  ARM B FAILED: the /*51*/ cdevsw row in $K does not carry &z3660ethinfo." >&2
	echo "$ROW51" | sed 's/^/    was: /' >&2
fi
cat >&2 <<EOF
  $K is INCONSISTENT and must not be relinked -- a row without its extern will not
  compile, an extern without its row links a driver no /dev node can reach.
  The pre-edit copy is $K.bnkorig ; restore it with
    cp $K.bnkorig $K
  then fix the tree by hand (BUILD.md step 2) and re-run.
EOF
exit 5
