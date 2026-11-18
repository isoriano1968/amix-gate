# pins.sh -- sourced by make-kit-030.sh and make-kit-040.sh: the upstream
# files as published at ../UPSTREAM's commit, unchanged, and the two checks
# both kits run.
(cd "$HERE/.." && sha256sum -c --quiet - <<'SUMS') || { echo "[FAIL] the upstream files are not the pinned ones"; exit 1; }
64ba6cedaa6c055be2413d492b46a69ea81dffe50939b487866dbad1f6473abb  src/a4091-wr.c
001dc20d3714f24ed5ed320a5f9d339e93ae71772da00b9acba15a674e12bba7  src/a4091_dmarec.h
20e6022dcc8109f79734098db83e2e3d22b72b342a628da822d4fbec44af74b4  LICENSE
aacb71960acee732f827733c61d10701b86cede3571261d50b9be9ac3bda6e07  tools/a4091-widths.py
SUMS

# a4091_gates OBJ "IMPORTS " -- what the object imports, what it defines, and
# upstream's register width check (8 and 32 bit only on the 53C710)
a4091_gates() {
	U=$($NM "$1" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
	[ "$U" = "$2" ] || { echo "[FAIL] $(basename "$1") imports: $U (expected $2)"; exit 1; }
	echo "[OK] imports: $U"
	for s in a4091q a4092q a4770q a4091queue a4091_sdcardbase; do
		$NM "$1" | grep -q " T $s\$" || { echo "[FAIL] $(basename "$1") does not define $s"; exit 1; }
	done
	python3 "$HERE/../tools/a4091-widths.py" "$1" --objdump "$(command -v "$OBJDUMP")" > "$B/widths.txt" || {
		cat "$B/widths.txt"; echo "[FAIL] 53C710 access widths"; exit 1; }
	echo "[OK] $(tail -1 "$B/widths.txt")"
}

a4091_card_check() {
	D=$($NM "$1" | awk '$2=="D"{print $3}' | tr '\n' ' ')
	U=$($NM "$1" | awk '$1=="U"{print $2}' | LC_ALL=C sort | tr '\n' ' ')
	[ "$D" = "a4091_scsicard " ] && [ "$U" = "a2090queue a2091queue a3091queue a4091q a4092q a4770q " ] || {
		echo "[FAIL] $(basename "$1") defines $D, imports $U"; exit 1; }
}
