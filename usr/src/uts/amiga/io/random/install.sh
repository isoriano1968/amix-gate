#!/bin/sh
# install.sh PORTDIR -- copy the /dev/random driver files into a checkout of
# amix-040-060-port (https://github.com/asokero/amix-040-060-port).
# Refuses to overwrite a file that exists with different contents; -f does.
set -e
force=0
[ "$1" = -f ] && { force=1; shift; }
PORT=$1
[ -n "$PORT" ] && [ -f "$PORT/relink-040.sh" ] && [ -f "$PORT/tools/config-load.sh" ] || {
	echo "usage: $0 [-f] /path/to/amix-040-060-port"; exit 1; }
cd "$(dirname "$0")/port"
for f in $(find . -type f | sed 's|^\./||' | sort); do
	if [ -f "$PORT/$f" ] && ! cmp -s "$f" "$PORT/$f" && [ $force = 0 ]; then
		echo "exists and differs: $PORT/$f (-f to overwrite)"; exit 1
	fi
done
for f in $(find . -type f | sed 's|^\./||' | sort); do
	mkdir -p "$PORT/$(dirname "$f")"
	cp "$f" "$PORT/$f"
	echo "  $f"
done
echo "installed into $PORT"
