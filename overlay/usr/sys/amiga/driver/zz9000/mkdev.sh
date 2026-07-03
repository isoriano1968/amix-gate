#!/bin/sh
#
# Create the AMIX device node for the ZZ9000 framebuffer driver.
# This matches the documented cdevsw major 49 integration.

rm -f /dev/zz9000
mknod /dev/zz9000 c 49 0
chmod 666 /dev/zz9000
ls -l /dev/zz9000
