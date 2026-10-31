#!/bin/bash
# patch-selftest.sh -- host-side, BENCHLESS self-test for the two on-box patch scripts
# (tools/patch-kernel.sh, tools/patch-makefile.sh).  No Amix box, no emulator, no build.
#
# The scripts under test must stay pre-POSIX-Bourne-sh + SVR4-ed portable; THIS driver
# does not -- it is bash, host-side only, and never ships to the box.  It re-runs the
# whole battery under every plain shell it can find (sh, dash, busybox sh) so a bashism
# that slipped into either script fails here rather than on the box.
#
# The battery is the workspace half-patch audit (docs/half-patch-audit-2026-07-31.md)
# turned into fixtures.  Every refusal case below is a shape that was MEASURED to pass
# silently through the pre-hardening scripts:
#
#   F1  pre-renumber tree (/*48=z3660eth*/ claimed, /*51*/ free): the old name-wide
#       `grep z3660eth` guard said ALREADY PATCHED and slot 51 stayed unclaimed.
#   F5  respelled `extern struct streamtab aeninfo;` anchor: ed missed, the extern was
#       never inserted, the /*51*/ row was claimed anyway, the script printed PATCHED
#       with rc 0, and its own guard reported ALREADY PATCHED on every later run.
#   F5  Makefile with the stock "# kdb/exp" comment stripped: ed missed the OBJ-list
#       anchor, the unconditional append still added the build rule, the name grep was
#       satisfied forever, and the driver never entered OBJ.
#
# Plus the two states the old scripts LEFT BEHIND (extern-only / rule-only), which the
# hardened scripts must now REPAIR rather than report as done, and the post-verify
# mechanism itself, exercised through an `ed` shim that deliberately drops an arm.
#
# Exit 0 = all green; non-zero on the first failure.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PK="$HERE/patch-kernel.sh"
PM="$HERE/patch-makefile.sh"
[ -f "$PK" ] || { echo "missing $PK" >&2; exit 2; }
[ -f "$PM" ] || { echo "missing $PM" >&2; exit 2; }
REALED="$(command -v ed)" || { echo "no ed(1) on this host -- cannot self-test" >&2; exit 2; }

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
fail() { echo "SELFTEST FAIL: $*" >&2; exit 1; }
ok()   { echo "  ok: $*"; }

# The replacement cdevsw row, lifted from the script under test exactly the way
# kerntools tools/cdevsw-gate-selftest.sh lifts it (intended cross-repo coupling: if the
# row is ever reshaped, both self-tests must move with it).
ROWRE='^ND,.*&z3660ethinfo,.*/\*51=z3660eth\*/'
NROW="$(grep -cE "$ROWRE" "$PK" || true)"
[ "$NROW" -eq 1 ] || fail "patch-kernel.sh carries $NROW lines matching kerntools' drift-guard regex $ROWRE (need exactly 1, at column 0) -- ../amix-kerntools/tools/cdevsw-gate-selftest.sh would fail"
OURSROW="$(grep -E "$ROWRE" "$PK")"

# ---------------------------------------------------------------- fixtures ------
# A faithful slice of AMIX 2.1 master.d/kernel.c: the real extern block spelling, the
# real row spelling (ten ND columns, notty, d_str, flag, two tabs, tag), and a bdevsw
# DECOY tagged /*51*/ ahead of the cdevsw table.  bdevsw tops out at 31 so that
# collision cannot really occur -- it is here because the old ed address
# (/\/\*51\*\//) would have landed on it, and the hardened one must not.
stock_kernel() {
	cat <<'XEOF'
extern miread();
extern struct streamtab aeninfo;
extern struct streamtab sldinfo;
extern struct streamtab loopinfo;

struct bdevsw bdevsw[] = {
ND,ND,ND,ND,ND,ND,ND,nullflag,	/*30*/
ND,ND,ND,ND,ND,ND,ND,nullflag,	/*51*/
};

struct cdevsw cdevsw[] = {
ND,ND,ND,ND,ND,ND,ND,ND,ND,ND,notty,&aeninfo,nullflag,		/*18=aen*/
ND,ND,ND,ND,ND,ND,ND,ND,ND,ND,notty,nostr,nullflag,		/*47*/
ND,ND,ND,ND,ND,ND,ND,ND,ND,ND,notty,nostr,nullflag,		/*48*/
ND,ND,ND,ND,ND,ND,ND,ND,ND,ND,notty,nostr,nullflag,		/*50*/
ND,ND,ND,ND,ND,ND,ND,ND,ND,ND,notty,nostr,nullflag,		/*51*/
ND,ND,ND,ND,ND,ND,ND,ND,ND,ND,notty,nostr,nullflag,		/*52*/
};
XEOF
}

# A faithful slice of amiga/driver/Makefile: the OBJ list ends with the un-backslashed
# "aen/exp # kdb/exp" entry, which is the anchor arm A depends on.
stock_makefile() {
	cat <<'XEOF'
CFLAGS	= -O -D_KERNEL -DSVR40 -DSVR4 -I../.. -I../inc

OBJ	= \
	  acia.o\
	  amiga.o\
	  tiga.o \
	  audio.o \
	  aen/exp # kdb/exp

exp :	$(OBJ)
	ld -r -o exp $(OBJ)

aen/exp :
	cd aen; $(MAKE)

kdb/exp :
	cd kdb; $(MAKE)
XEOF
}

T=$'\t'
CLAIMED48="ND,ND,ND,ND,ND,ND,ND,ND,ND,ND,notty,&z3660ethinfo,nullflag,$T$T/*48=z3660eth*/"
FREE51="ND,ND,ND,ND,ND,ND,ND,ND,ND,ND,notty,nostr,nullflag,$T$T/*51*/"
TAKEN51="ND,ND,ND,ND,ND,ND,ND,ND,ND,ND,notty,&randominfo,nullflag,$T$T/*51*/"
NAMED51="ND,ND,ND,ND,ND,ND,ND,ND,ND,ND,notty,nostr,nullflag,$T$T/*51=kdebug*/"
RETAG51="ND,ND,ND,ND,ND,ND,ND,ND,ND,ND,notty,&mxinfo,nullflag,$T/* 51 jck MAX-125 */"
EXTLINE='extern struct streamtab z3660ethinfo;'
RESPELLED="extern${T}struct streamtab aeninfo;"
# EREs, not literals: the row text is full of regex metacharacters (/*51*/) and its
# replacement is full of sed's & -- awk with -v keeps both sides literal.
RE_AEN='^extern struct streamtab aeninfo;$'
RE_48='^ND,.*nostr,nullflag,.*/[*]48[*]/$'
RE_51='^ND,.*nostr,nullflag,.*/[*]51[*]/$'

subst()  { awk -v re="$2" -v new="$3" '$0 ~ re { print new; next } { print }' "$1" > "$1.t" && mv "$1.t" "$1"; }
after()  { awk -v re="$2" -v new="$3" '{ print } $0 ~ re { print new }'       "$1" > "$1.t" && mv "$1.t" "$1"; }

# kfix <name> -> path to a kernel.c fixture of that shape
kfix() {
	local f="$D/$1.c"
	stock_kernel > "$f"
	case "$1" in
	stock) ;;
	prerenumber)                     # F1: our name claimed at the OLD row, 51 still free
		subst "$f" "$RE_48" "$CLAIMED48"
		after "$f" "$RE_AEN" "$EXTLINE" ;;
	respelled)                       # F5: arm A's anchor respelled -- ed would miss it
		subst "$f" "$RE_AEN" "$RESPELLED" ;;
	taken)                           # a foreign claim keeping the bare tag
		subst "$f" "$RE_51" "$TAKEN51" ;;
	named)                           # a named-but-empty claim
		subst "$f" "$RE_51" "$NAMED51" ;;
	retagged)                        # MAX-125 style, no "=" -- ed's search would miss
		subst "$f" "$RE_51" "$RETAG51" ;;
	ambiguous)                       # two rows carry the tag
		after "$f" "$RE_51" "$FREE51" ;;
	externonly)                      # the state a failed arm B leaves
		after "$f" "$RE_AEN" "$EXTLINE" ;;
	rowonly)                         # the state a failed arm A leaves (measured, F5)
		subst "$f" "$RE_51" "$OURSROW" ;;
	rowonly_noanchor)                # ... on the tree that caused it: anchor respelled
		subst "$f" "$RE_51" "$OURSROW"
		subst "$f" "$RE_AEN" "$RESPELLED" ;;
	*) fail "unknown kernel fixture $1" ;;
	esac
	echo "$f"
}

# mfix <name> -> path to a Makefile fixture of that shape
mfix() {
	local f="$D/Makefile.$1"
	stock_makefile > "$f"
	case "$1" in
	stock) ;;
	nocomment)                       # F5: the anchor comment stripped
		sed -i 's|^\(\t  aen/exp\) # kdb/exp$|\1|' "$f" ;;
	ruleonly)                        # exactly what the old script left on 'nocomment'
		sed -i 's|^\(\t  aen/exp\) # kdb/exp$|\1|' "$f"
		printf '\nz3660eth/exp :\n\tcd z3660eth; $(MAKE)\n' >> "$f" ;;
	ruleonly_anchored)               # rule-only, but the anchor is intact -> repairable
		printf '\nz3660eth/exp :\n\tcd z3660eth; $(MAKE)\n' >> "$f" ;;
	objonly)                         # OBJ entry present, rule missing -> repairable
		sed -i 's|^\(\t  aen/exp # kdb/exp\)$|\t  z3660eth/exp \\\n\1|' "$f" ;;
	twoanchors)
		sed -i 's|^\(\t  aen/exp # kdb/exp\)$|\1\n\1|' "$f" ;;
	*) fail "unknown makefile fixture $1" ;;
	esac
	echo "$f"
}

# ------------------------------------------------------------------ drivers -----
RC=0; OUT=""
run() {  # run <script> <fixture> [PATH-override]
	RC=0
	if [ $# -ge 3 ]; then
		OUT="$(PATH="$3" "$SH" "$1" "$2" 2>&1)" || RC=$?
	else
		OUT="$("$SH" "$1" "$2" 2>&1)" || RC=$?
	fi
}
want_rc()  { [ "$RC" -eq "$1" ] || fail "$2: expected rc $1, got $RC -- output: $OUT"; }
want_out() { case "$OUT" in *"$1"*) : ;; *) fail "$2: output lacks '$1' -- output: $OUT";; esac; }
want_not() { case "$OUT" in *"$1"*) fail "$2: output must not contain '$1' -- output: $OUT";; *) : ;; esac; }
untouched() { cmp -s "$1" "$2" || { diff "$1" "$2" || true; fail "$3: the file was MODIFIED but the run must not touch it"; }; }

# not_already <label> -- the run must not have DECLARED the no-op outcome.  (The refusal
# texts quote the phrase "reported ALREADY PATCHED" while explaining the old bug, so the
# check is on the declaration token, not on the words.)
not_already() {
	case "$OUT" in
	*"KERNEL.C ALREADY PATCHED"*|*"MAKEFILE ALREADY PATCHED"*)
		fail "$1: declared ALREADY PATCHED -- output: $OUT" ;;
	esac
}

# refuse <label> <script> <fixture> <phrase>  -- loud, rc 4, and the file is untouched
refuse() {
	local label="$1" script="$2" fx="$3" phrase="$4"
	cp "$fx" "$fx.pristine"
	run "$script" "$fx"
	want_rc 4 "$label"
	case "$(echo "$OUT" | head -1)" in
	*"REFUSED ("*) : ;;
	*) fail "$label: the refusal is not the FIRST thing said -- output: $OUT" ;;
	esac
	want_out "$phrase" "$label"
	not_already "$label"
	untouched "$fx.pristine" "$fx" "$label"
	ok "$label -> REFUSED (rc 4, '$phrase'), file untouched"
}

# -------------------------------------------------------------- the battery -----
battery() {
	echo "== battery under SH=$SH =="
	D="$TMP/$(basename "$SH")"; rm -rf "$D"; mkdir -p "$D"

	# --- 1. kernel.c: clean apply on a stock tree -------------------------------
	f="$(kfix stock)"; cp "$f" "$f.before"
	run "$PK" "$f"
	want_rc 0 "clean apply"; want_out "KERNEL.C PATCHED:" "clean apply"
	[ "$(grep -c "^$EXTLINE\$" "$f")" -eq 1 ] || fail "clean apply: extern not inserted exactly once"
	grep -A1 '^extern struct streamtab aeninfo;$' "$f" | tail -1 | grep -qxF "$EXTLINE" \
		|| fail "clean apply: extern is not immediately after the stock aen one"
	got="$(grep notty "$f" | grep '/\*51[*=]')"
	[ "$got" = "$OURSROW" ] || { diff <(echo "$OURSROW") <(echo "$got") || true; fail "clean apply: the emitted row is not byte-identical to the script's row"; }
	grep -qxF 'ND,ND,ND,ND,ND,ND,ND,nullflag,	/*51*/' "$f" || fail "clean apply: the bdevsw decoy row tagged /*51*/ was clobbered"
	[ "$(grep -c 'z3660eth' "$f")" -eq 2 ] || fail "clean apply: expected exactly 2 z3660eth lines"
	ok "clean apply: extern after the aen one, /*51*/ row byte-exact, bdevsw decoy intact"

	# --- 2. kernel.c: idempotent re-run -----------------------------------------
	cp "$f" "$f.applied"
	run "$PK" "$f"
	want_rc 0 "idempotent re-run"; want_out "KERNEL.C ALREADY PATCHED" "idempotent re-run"
	untouched "$f.applied" "$f" "idempotent re-run"
	ok "idempotent re-run -> ALREADY PATCHED (rc 0), file byte-identical"

	# --- 3. the three audit shapes: refused, loudly and distinctly ---------------
	refuse "F1 pre-renumber tree (/*48=z3660eth*/ claimed)" "$PK" "$(kfix prerenumber)" "(MOVED)"
	refuse "F5 respelled aeninfo anchor"                    "$PK" "$(kfix respelled)"   "(NOANCHOR)"
	refuse "F5 Makefile with the anchor comment stripped"   "$PM" "$(mfix nocomment)"   "(NOANCHOR)"

	# --- 4. the rest of the cdevsw verdict vocabulary ---------------------------
	refuse "foreign claim keeping the bare tag" "$PK" "$(kfix taken)"     "(TAKEN)"
	refuse "named-but-empty claim /*51=kdebug*/" "$PK" "$(kfix named)"     "(TAKEN)"
	refuse "retagged MAX-125-style claim"        "$PK" "$(kfix retagged)"  "(MISSING)"
	refuse "two rows tagged /*51*/"              "$PK" "$(kfix ambiguous)" "(AMBIGUOUS)"
	run "$PK" "$D/does-not-exist.c"
	want_rc 4 "missing kernel.c"; want_out "(NOKERNELC)" "missing kernel.c"
	ok "missing kernel.c -> REFUSED (NOKERNELC)"

	# --- 5. kernel.c: the half states the OLD script left are REPAIRED ----------
	f="$(kfix externonly)"
	run "$PK" "$f"
	want_rc 0 "repair extern-only"; want_out "KERNEL.C PATCHED:" "repair extern-only"
	[ "$(grep -c "^$EXTLINE\$" "$f")" -eq 1 ] || fail "repair extern-only: the extern was duplicated"
	grep notty "$f" | grep '/\*51[*=]' | grep -qF '&z3660ethinfo' || fail "repair extern-only: row not claimed"
	ok "extern-only half state -> repaired (row added, extern NOT duplicated)"

	f="$(kfix rowonly)"
	run "$PK" "$f"
	want_rc 0 "repair row-only"; want_out "KERNEL.C PATCHED:" "repair row-only"
	[ "$(grep -c "^$EXTLINE\$" "$f")" -eq 1 ] || fail "repair row-only: extern not added exactly once"
	ok "row-only half state (what the old script left) -> repaired (extern added)"

	refuse "row-only half state on the tree that caused it" "$PK" "$(kfix rowonly_noanchor)" "(NOANCHOR)"

	# --- 6. Makefile: clean apply, idempotent re-run ----------------------------
	f="$(mfix stock)"
	run "$PM" "$f"
	want_rc 0 "makefile clean apply"; want_out "MAKEFILE PATCHED:" "makefile clean apply"
	grep -qxF '	  z3660eth/exp \' "$f" || fail "makefile clean apply: OBJ entry missing or reshaped"
	grep -qxF 'z3660eth/exp :' "$f"      || fail "makefile clean apply: build rule missing"
	grep -qxF '	cd z3660eth; $(MAKE)' "$f" || fail "makefile clean apply: rule body missing or reshaped"
	grep -B1 '^	  aen/exp # kdb/exp$' "$f" | head -1 | grep -qxF '	  z3660eth/exp \' \
		|| fail "makefile clean apply: OBJ entry is not immediately before the aen/exp anchor"
	ok "makefile clean apply: OBJ entry before the anchor + rule appended, both byte-exact"
	cp "$f" "$f.applied"
	run "$PM" "$f"
	want_rc 0 "makefile re-run"; want_out "MAKEFILE ALREADY PATCHED" "makefile re-run"
	untouched "$f.applied" "$f" "makefile re-run"
	ok "makefile idempotent re-run -> ALREADY PATCHED (rc 0), file byte-identical"

	# --- 7. Makefile: the rule-only half state the OLD script created ------------
	# On the tree that caused it (anchor gone) it must be REFUSED and diagnosed as such;
	# on a tree whose anchor is intact it must be REPAIRED, without a second rule.
	f="$(mfix ruleonly)"; cp "$f" "$f.pristine"
	run "$PM" "$f"
	want_rc 4 "rule-only half state"; want_out "(NOANCHOR)" "rule-only half state"
	want_out "ALREADY carries the z3660eth build rule but no OBJ entry" "rule-only half state"
	not_already "rule-only half state"
	untouched "$f.pristine" "$f" "rule-only half state"
	ok "rule-only half state (anchor gone) -> REFUSED (rc 4) and named as that half state"

	f="$(mfix ruleonly_anchored)"
	run "$PM" "$f"
	want_rc 0 "repair rule-only"; want_out "MAKEFILE PATCHED:" "repair rule-only"
	[ "$(grep -c '^z3660eth/exp[ 	]*:' "$f")" -eq 1 ] || fail "repair rule-only: the build rule was duplicated"
	grep -qxF '	  z3660eth/exp \' "$f" || fail "repair rule-only: OBJ entry not added"
	ok "rule-only half state (anchor intact) -> repaired (OBJ entry added, rule NOT duplicated)"

	f="$(mfix objonly)"
	run "$PM" "$f"
	want_rc 0 "repair obj-only"; want_out "MAKEFILE PATCHED:" "repair obj-only"
	[ "$(grep -c '^	  z3660eth/exp \\$' "$f")" -eq 1 ] || fail "repair obj-only: the OBJ entry was duplicated"
	grep -qxF 'z3660eth/exp :' "$f" || fail "repair obj-only: rule not added"
	ok "obj-only half state -> repaired (rule added, OBJ entry NOT duplicated)"

	refuse "two OBJ-list anchors" "$PM" "$(mfix twoanchors)" "(NOANCHOR)"

	# --- 8. the post-verify mechanism itself, via an ed shim --------------------
	# SVR4 ed reports an address miss with a bare `?`, keeps reading, writes anyway and
	# exits 0 -- so the ONLY thing standing between a miss and a silent half-patch is the
	# post-verify.  Prove it fires by giving the script an ed that drops arm(s).
	mkdir -p "$D/bin"
	shim() {
		cat > "$D/bin/ed" <<EOS
#!/bin/sh
# selftest shim: mode=$1.  A real SVR4 ed on a miss also exits 0 having written nothing
# useful; that is exactly what "none" imitates.
in=\`cat\`
case "$1" in
none) exit 0 ;;
armA)
	case "\$in" in
	*"$EXTLINE"*) echo "\$in" | "$REALED" "\$@" ;;
	*) exit 0 ;;
	esac ;;
esac
exit 0
EOS
		chmod +x "$D/bin/ed"
	}

	shim none
	f="$(kfix stock)"
	run "$PK" "$f" "$D/bin:$PATH"
	want_rc 5 "kernel HALF (both arms dropped)"
	want_out "KERNEL.C HALF-PATCHED" "kernel HALF (both arms dropped)"
	want_out "ARM A FAILED" "kernel HALF (both arms dropped)"
	want_out "ARM B FAILED" "kernel HALF (both arms dropped)"
	want_out "$f.bnkorig" "kernel HALF (both arms dropped)"
	[ -f "$f.bnkorig" ] || fail "kernel HALF: no .bnkorig backup to restore from"
	ok "both arms dropped -> HALF-PATCHED (rc 5), both arms named, backup present"

	shim armA
	f="$(kfix stock)"
	run "$PK" "$f" "$D/bin:$PATH"
	want_rc 5 "kernel HALF (arm B dropped)"
	want_out "ARM B FAILED" "kernel HALF (arm B dropped)"
	want_not "ARM A FAILED" "kernel HALF (arm B dropped)"
	ok "arm B dropped only -> HALF-PATCHED (rc 5) naming ARM B, not ARM A"

	shim none
	f="$(mfix stock)"
	run "$PM" "$f" "$D/bin:$PATH"
	want_rc 5 "makefile HALF"
	want_out "MAKEFILE HALF-PATCHED" "makefile HALF"
	want_out "ARM A FAILED" "makefile HALF"
	want_out "ARM B FAILED" "makefile HALF"
	ok "makefile arms dropped -> HALF-PATCHED (rc 5), both arms named"
}

# ------------------------------------------------------- portability lint -------
echo "== 0. portability lint: the two scripts must stay pre-POSIX-sh clean =="
# Two things are stripped before linting, both because they are not shell:
#   * full-line comments -- they legitimately NAME the banned constructs ("no printf(1)",
#     "no [[") and must not trip their own lint;
#   * QUOTED here-document bodies (<<'EOF') -- literal payload handed to ed, which is why
#     the Makefile rule body may contain $(MAKE).  UNQUOTED bodies (cat <<EOF) are left in:
#     they DO undergo expansion, so a $(...) there would be real command substitution.
strip_payload() {
	awk '
		/<<.EOF.$/  { q = 1; next }
		q && $0 == "EOF" { q = 0; next }
		q           { next }
		/^[ \t]*#/  { next }
		{ print }
	' "$1"
}
BASHISM='\$\(|\[\[|\]\]|^[[:space:]]*local[[:space:]]|[A-Za-z_]+\+=|<<<|&>|\$\{[A-Za-z_0-9]+[#%/^,]|^[[:space:]]*function[[:space:]]|echo[[:space:]]+-[neE]|grep[[:space:]]+-[a-zA-Z]*[qE]|sed[[:space:]]+-[a-zA-Z]*[iE]|[^-a-zA-Z_]printf[[:space:]]|\bpushd\b|^[[:space:]]*source[[:space:]]'
for f in "$PK" "$PM"; do
	b="$(basename "$f")"
	sh -n "$f"   || fail "$b: sh -n syntax error"
	bash -n "$f" || fail "$b: bash -n syntax error"
	if hits="$(strip_payload "$f" | grep -E "$BASHISM" || true)"; [ -n "$hits" ]; then
		echo "$hits" >&2; fail "$b: non-Bourne construct(s) above"
	fi
	head -1 "$f" | grep -qxF '#!/bin/sh' || fail "$b: shebang is not #!/bin/sh"
	ok "$b: sh -n + bash -n clean, no \$(), [[, local, arrays, printf, grep -q/-E, sed -i/-E"
done
for extra in dash "busybox sh"; do
	set -- $extra
	command -v "$1" >/dev/null 2>&1 || { ok "$extra not installed -- skipping its -n pass"; continue; }
	for f in "$PK" "$PM"; do
		$extra -n "$f" || fail "$(basename "$f"): $extra -n syntax error"
	done
	ok "$extra -n clean for both scripts"
done

# ------------------------------------------------------------ run the battery ---
# busybox sh is deliberately NOT in this list even though its `-n` pass runs above: with
# FEATURE_SH_STANDALONE it resolves `ed` to its own applet, and that applet rejects the
# `ed - file` silent flag (SVR4 ed and GNU ed both take it).  That is a busybox
# limitation, not a portability defect in the scripts -- so it lints them but cannot host
# the battery.
SHELLS=(sh)
command -v dash >/dev/null 2>&1 && SHELLS+=(dash)
for SH in "${SHELLS[@]}"; do battery; done

# ---------------------------------------------- the real staged tree, if present --
# Opportunistic: when a kerntools cross-kernel staging tree happens to be lying around,
# assert the pre-flight classifies the REAL 14 KB kernel.c the way its shape says it
# should.  Never required -- the synthetic battery above is the contract.
echo "== real staged tree (opportunistic) =="
SH=sh
REALKC=""
for c in "$HERE"/../../amix-kerntools/work/*/usr/sys/master.d/kernel.c; do
	[ -f "$c" ] && { REALKC="$c"; break; }
done
if [ -z "$REALKC" ]; then
	ok "no staged kernel.c under ../amix-kerntools/work -- skipped (synthetic battery stands alone)"
else
	D="$TMP/real"; mkdir -p "$D"; cp "$REALKC" "$D/kernel.c"
	n="$(grep notty "$D/kernel.c" | grep -c '/\*51[*=]' || true)"
	[ "$n" -eq 1 ] || fail "real $REALKC: the row-scoped probe found $n rows tagged /*51*/, expected 1"
	ok "real kernel.c ($REALKC): the probe finds exactly 1 cdevsw row tagged /*51*/"
	stale="$(grep notty "$D/kernel.c" | grep '&z3660ethinfo' | grep -v '/\*51[*=]' || true)"
	cp "$D/kernel.c" "$D/kernel.c.pristine"
	run "$PK" "$D/kernel.c"
	if [ -n "$stale" ]; then
		want_rc 4 "real tree (pre-renumber shape)"; want_out "(MOVED)" "real tree (pre-renumber shape)"
		untouched "$D/kernel.c.pristine" "$D/kernel.c" "real tree (pre-renumber shape)"
		ok "real tree carries a stale non-51 z3660eth claim -> REFUSED (MOVED), untouched"
	else
		want_rc 0 "real tree"
		ok "real tree has no stale claim -> rc 0 ($(echo "$OUT" | head -1))"
	fi
fi

echo "SELFTEST PASS"
