#!/bin/sh
# install.sh PORTDIR -- copy the rtgfb driver files into a checkout of
# amix-040-060-port (https://github.com/asokero/amix-040-060-port).
# Refuses to overwrite a file that exists with different contents; -f does.
set -e
force=0
[ "$1" = -f ] && { force=1; shift; }
PORT=$1
[ -n "$PORT" ] && [ -f "$PORT/relink-040.sh" ] && [ -f "$PORT/tools/config-load.sh" ] || {
	echo "usage: $0 [-f] /path/to/amix-040-060-port"; exit 1; }
cd "$(dirname "$0")/port"
# rtgfb's own files, and the driver-chain pieces every kit shares, from
# amix-gate's port tree (src/patch_io_init.py, src/amixdrv_init.s.in,
# tools/kit-lib.sh)
GATE=../../../port
FILES=$(find . -path ./kit -prune -o -path ./build -prune -o -name .gitignore -prune -o -type f -print | sed 's|^\./||' | sort)
SHARED="src/patch_io_init.py src/amixdrv_init.s.in tools/kit-lib.sh"
for f in $FILES $SHARED; do
	s=$f; [ -f "$f" ] || s=$GATE/$f
	if [ -f "$PORT/$f" ] && ! cmp -s "$s" "$PORT/$f" && [ $force = 0 ]; then
		echo "exists and differs: $PORT/$f (-f to overwrite)"; exit 1
	fi
done
for f in $FILES $SHARED; do
	s=$f; [ -f "$f" ] || s=$GATE/$f
	mkdir -p "$PORT/$(dirname "$f")"
	cp "$s" "$PORT/$f"
	echo "  $f"
done
echo "installed into $PORT"
