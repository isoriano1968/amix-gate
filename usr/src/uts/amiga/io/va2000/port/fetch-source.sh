# fetch-source.sh -- sourced by make-kit-030.sh and make-kit-040.sh: the VA2000
# driver source, asokero's va2000-amix (MIT), pinned, plus patches/ applied.
#
#   VA2000_SRC_DIR  a checkout or unpacked tree of va2000-amix at the pinned
#                   commit (its src/va2000.c and src/va2000.h are checked);
#                   left out: the pinned archive is downloaded into build/.
# va2000_fetch BUILD: VA2000_TREE = BUILD/va2000-src: src/va2000.c (pristine,
# checked against the pin), src/va2000.h, src/LICENSE.
# va2000_patch FILE: applies patches/*.patch to FILE (a copy of va2000.c).
VA2000_REPO=https://github.com/asokero/va2000-amix
VA2000_COMMIT=7f82edcc3db9ddb0f28173ee41abe6c439e98327
VA2000_C_SHA256=b542e9019ae37fba2452e6833e77bbe1364298c1d17921255b6b6836364b424b
VA2000_H_SHA256=65fb8d77fe3ab6a4dd4968767f4276080419a0d5c740e1fda29ebe73f4955e79

va2000_fetch() {
	_b=$1
	rm -rf "$_b/va2000-src"; mkdir -p "$_b/va2000-src/src"
	_from=${VA2000_SRC_DIR:-}
	if [ -z "$_from" ]; then
		command -v curl >/dev/null || { echo "no curl: set VA2000_SRC_DIR" >&2; exit 1; }
		curl -fsSL -o "$_b/va2000-amix.tar.gz" "$VA2000_REPO/archive/$VA2000_COMMIT.tar.gz"
		mkdir -p "$_b/va2000-dl"
		tar xzf "$_b/va2000-amix.tar.gz" -C "$_b/va2000-dl"
		_from="$_b/va2000-dl/va2000-amix-$VA2000_COMMIT"
	fi
	cp "$_from/src/va2000.c" "$_from/src/va2000.h" "$_from/LICENSE" "$_b/va2000-src/src/"
	echo "$VA2000_C_SHA256  $_b/va2000-src/src/va2000.c" | sha256sum -c --quiet &&
	echo "$VA2000_H_SHA256  $_b/va2000-src/src/va2000.h" | sha256sum -c --quiet || {
		echo "[FAIL] va2000.c/.h are not those of va2000-amix $VA2000_COMMIT" >&2; exit 1; }
	rm -rf "$_b/va2000-dl" "$_b/va2000-amix.tar.gz"
	VA2000_TREE="$_b/va2000-src"
	echo "[OK] va2000-amix $VA2000_COMMIT"
}

va2000_patch() {
	for _p in "$HERE/../patches"/*.patch; do
		patch -s "$1" < "$_p" || { echo "[FAIL] $_p" >&2; exit 1; }
		echo "[OK] $(basename "$_p")"
	done
}
