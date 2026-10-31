#!/usr/bin/env python3
"""patch_audio_paula.py -- /dev/audio and /dev/paula exclude each other.
Applied to a build-time copy of the stock AMIX 2.1c audio driver
(usr/sys/amiga/driver/audio.c).  Usage: patch_audio_paula.py audio.c

/dev/paula (src/paula.c, major 53) takes Paula's four channels and drives
their registers directly; /dev/audio feeds the same channels from its own
buffers and interrupts.  They must never run at once.  This adds two
globals to audio.c, which is linked before paula.o:

  paula_busy   set by paulaopen(); audioopen() then fails with EBUSY
  audio_nopen  /dev/audio clients now open; paulaopen() fails with EBUSY
               while it is not zero

The count goes up where audioopen() puts a client on the driver's client
list and down where audioclose() takes it off.  Both run in process context
on a uniprocessor kernel, so a plain int is enough.

/dev/audio (c 46,0) is one STREAMS device, not a clone: every process that
opens it shares one stream.  STREAMS calls audioopen() on every open() of
that stream but audioclose() only on its last close, and the stock
audioopen() gives each call a new client and points the stream at it.  A
second opener would take the first one's stream over and, once counted
here, never be counted down: /dev/paula would refuse everyone until a
reboot.  So audioopen() also refuses a stream that is already open
(EBUSY): one client at a time, which is all one stream can carry.

The source is Commodore's and is not in this repository, and neither is
any of its text.  Each place is found by a fingerprint -- the number of
lines and the SHA-256 of those lines exactly as the stock file has them --
which must match exactly once; our lines go in after it:

  1. the declaration of the client list head      -> the two globals
  2. audioopen(): the refusal of a module open    -> the two EBUSY tests
  3. audioopen(): the client joins the list       -> audio_nopen++
  4. audioclose(): the client leaves the list     -> audio_nopen--
"""
import hashlib
import sys

PLACES = [
    # (what, lines, sha256 of those lines, our lines to insert after them)
    ("client list declaration", 1,
     "72aeadf245332892dcf256b8d1a10f73e4c795fe7207431a596dd1578e1d1bef",
     ["int paula_busy;\t\t/* /dev/paula holds the channels (src/paula.c) */",
      "int audio_nopen;\t/* clients on the list: /dev/paula waits for 0 */"]),
    ("audioopen: module open refused", 2,
     "50f82981f433b6f645666ff58c60ed12b0ad392a13fe55d188792c1e55c8eafc",
     ["    if (paula_busy)\t/* /dev/paula has the channels */",
      "\treturn EBUSY;",
      "    if (q->q_ptr != NULL)\t/* the one stream is someone's already */",
      "\treturn EBUSY;"]),
    ("audioopen: client joins the list", 1,
     "6d4aa0e4693fbd2dc350b1a03f9053e937b5b6a6acf6ca2637af6d769cd3d446",
     ["    audio_nopen++;"]),
    ("audioclose: client leaves the list", 1,
     "ad5e45679fcff3091ef7f584a750ca0f81578b1606320bbcf9f6dc868a18bba1",
     ["    audio_nopen--;"]),
]


def find(lines, n, digest, what):
    hits = [i for i in range(len(lines) - n + 1)
            if hashlib.sha256("".join(lines[i:i + n]).encode("latin-1")).hexdigest() == digest]
    if len(hits) != 1:
        sys.exit(f"[FAIL] audio.c: {what}: fingerprint found {len(hits)} times, expected 1 "
                 "(not the stock AMIX 2.1c audio.c?)")
    return hits[0]


path = sys.argv[1]
# split at newlines only: the stock file has form feeds between functions,
# which splitlines() would treat as line ends as well
lines = open(path, encoding="latin-1", newline="").read().split("\n")
lines = [l + "\n" for l in lines[:-1]] + ([lines[-1]] if lines[-1] else [])
if any("paula_busy" in l for l in lines):
    sys.exit("[FAIL] audio.c already carries the /dev/paula exclusion")

# locate everything first, then insert from the bottom up so positions hold
at = sorted(((find(lines, n, d, what) + n, new) for what, n, d, new in PLACES), reverse=True)
for pos, new in at:
    lines[pos:pos] = [l + "\n" for l in new]
open(path, "w", encoding="latin-1").write("".join(lines))
print("      [ok] audio.c: paula_busy / audio_nopen exclusion with /dev/paula")
