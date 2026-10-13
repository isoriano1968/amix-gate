#!/bin/sh
# Host preprocessor for the Amix-targeted imake: no host predefines (-undef),
# and the host imake's own "-Dlinux" (compiled in from its host build) dropped,
# so Imake.cf selects amix.cf only.  Everything else passes through.
args=""
for a in "$@"; do
	case "$a" in
	-Dlinux|-D__linux__|-Dunix) ;;
	*) args="$args \"$a\"" ;;
	esac
done
eval exec /usr/bin/cpp -undef $args
