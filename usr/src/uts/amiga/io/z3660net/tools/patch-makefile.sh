#!/bin/sh
# On-box integration of z3660eth into amiga/driver/Makefile (run on the AMIX box).
# Inserts "z3660eth/exp \" into the OBJ list before the aen/exp entry (which is the
# LAST entry, the one with no trailing backslash) + appends the build rule.  Uses ed.
# Idempotent.
#
# Usage: sh patch-makefile.sh [path-to-Makefile]   (default /usr/sys/amiga/driver/Makefile;
# the argument exists so tools/patch-selftest.sh can drive it over fixtures, benchless.)
#
# PORTABILITY -- pre-POSIX Bourne /bin/sh + SVR4 ed, same rules as patch-kernel.sh:
# backticks (never the $-paren form), no [[, no local, no arrays, no $-paren-paren
# arithmetic, no printf(1), no grep -q/-E, no sed -i/-E.  patch-selftest.sh lints it.
#
# TWO INDEPENDENT ARMS, each separately guarded, each separately post-verified:
#   A  the OBJ-list entry   (anchored on the stock "aen/exp # kdb/exp" line)
#   B  the build rule       (appended at end of file)
# The old script had ONE name-wide guard and an UNCONDITIONAL append, which produced
# the worst reachable state: on a tree whose stock "# kdb/exp" comment had been
# stripped, arm A's address search missed, arm B still appended the rule, `grep
# z3660eth` was satisfied forever, and the driver never entered OBJ.  nm -u then fails
# at link time and gets misattributed to the D245 ld corruption, while the fallback's
# precondition is satisfied by the rule alone so nothing ever repairs it.
# (workspace half-patch audit 2026-07-31, F5.)  Measured, not assumed: ed reports the
# miss with a bare `?`, keeps reading the here-document as commands, writes anyway,
# rc 0 -- so post-verifying the RESULT is the only check that holds.
#
# EXIT STATUS:
#   0  MAKEFILE ALREADY PATCHED   both arms already present; nothing written
#   0  MAKEFILE PATCHED:          every arm that was needed landed AND was verified
#   4  MAKEFILE REFUSED (<v>):    pre-flight said no; the file was NOT touched
#   5  MAKEFILE HALF-PATCHED:     an arm did not land; restore the backup, do not build
# A tree already in the rule-only half state is REPAIRED by this script, not reported
# as done: arm A is guarded on the OBJ entry, not on the driver name.

M=${1:-/usr/sys/amiga/driver/Makefile}

if test ! -f "$M" || test ! -r "$M"; then
	echo "MAKEFILE REFUSED (NOMAKEFILE): $M is missing or unreadable." >&2
	echo "  Nothing was written." >&2
	exit 4
fi

# ---------------------------------------------------------------- pre-flight ----
# The two arms are told apart by shape, not by name: an OBJ-list entry is indented and
# has no colon; the build rule starts in column zero and has one.  (The rule body,
# "cd z3660eth; ...", never contains the string z3660eth/exp.)
HAVEOBJ=no
grep '^[ 	][ 	]*z3660eth/exp' "$M" >/dev/null 2>&1 && HAVEOBJ=yes
HAVERULE=no
grep '^z3660eth/exp[ 	]*:' "$M" >/dev/null 2>&1 && HAVERULE=yes

if test "$HAVEOBJ" = yes && test "$HAVERULE" = yes; then
	echo "MAKEFILE ALREADY PATCHED"
	grep -n z3660eth "$M"
	exit 0
fi

# Arm A's anchor is checked with the SAME pattern ed searches for, so the pre-flight
# predicts ed exactly.  Exactly one occurrence, or we do not start -- and in particular
# we do NOT append the rule, because the rule alone is the state that hides forever.
if test "$HAVEOBJ" = no; then
	NANCHOR=`grep -c 'aen/exp #' "$M"`
	if test "$NANCHOR" -ne 1; then
		cat >&2 <<EOF
MAKEFILE REFUSED (NOANCHOR): the stock OBJ-list line containing
    aen/exp #
  occurs $NANCHOR times (need exactly 1) in
  $M
  Arm A inserts our OBJ entry immediately before it; the anchor needs that literal
  trailing "# kdb/exp" comment.  On a miss ed reads our OBJ line as an ed COMMAND and
  still writes the file, and appending the build rule anyway would satisfy a name grep
  forever while the driver never entered OBJ (audit F5).
  Add
    z3660eth/exp \\
  by hand as an OBJ entry (any position, keep the trailing backslash unless it is the
  last entry), then re-run.  Nothing was written.
EOF
		if test "$HAVERULE" = yes; then
			cat >&2 <<EOF
  NOTE: this Makefile ALREADY carries the z3660eth build rule but no OBJ entry -- it is
  in exactly that half state now.  Adding the OBJ entry by hand completes it; do not
  add a second rule.
EOF
		fi
		exit 4
	fi
fi

# ------------------------------------------------------------------- write ------
# $M.bnkorig is the state as this run found it (an earlier run's copy is replaced).
cp "$M" "$M.bnkorig"
chmod 644 "$M"

# Arm A -- the OBJ-list entry.  Its own ed invocation: a miss here must not reach arm B.
if test "$HAVEOBJ" = no; then
ed - "$M" <<'EOF'
/aen\/exp #/i
	  z3660eth/exp \
.
w
q
EOF
fi

# Arm B -- the build rule.
if test "$HAVERULE" = no; then
ed - "$M" <<'EOF'
$a

z3660eth/exp :
	cd z3660eth; $(MAKE)
.
w
q
EOF
fi

# ------------------------------------------------------------- post-verify -----
GOTOBJ=no
grep '^[ 	][ 	]*z3660eth/exp' "$M" >/dev/null 2>&1 && GOTOBJ=yes
GOTRULE=no
grep '^z3660eth/exp[ 	]*:' "$M" >/dev/null 2>&1 && GOTRULE=yes

if test "$GOTOBJ" = yes && test "$GOTRULE" = yes; then
	echo "MAKEFILE PATCHED:"
	grep -n z3660eth "$M"
	exit 0
fi

echo "MAKEFILE HALF-PATCHED: ed did not land every arm." >&2
if test "$GOTOBJ" = no; then
	echo "  ARM A FAILED: no indented z3660eth/exp entry in the OBJ list of $M." >&2
	echo "                The driver would never be linked into amiga/driver/exp;" >&2
	echo "                the symptom is an nm -u failure that looks like D245." >&2
fi
if test "$GOTRULE" = no; then
	echo "  ARM B FAILED: no 'z3660eth/exp :' build rule at column 0 in $M." >&2
fi
cat >&2 <<EOF
  $M is INCONSISTENT and must not be built.  The pre-edit copy is $M.bnkorig ;
  restore it with
    cp $M.bnkorig $M
  then fix the tree by hand (BUILD.md step 3) and re-run.
EOF
exit 5
