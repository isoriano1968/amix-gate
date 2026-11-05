#!/usr/bin/env python3
"""patch_audio_paula.py -- /dev/audio and /dev/paula exclude each other.
Applied after patch_audio_stereo.py to the build-time copy of audio.c.
Usage: patch_audio_paula.py audio.c

/dev/paula (src/paula.c, major 53) takes Paula's four channels and drives
their registers directly; /dev/audio feeds the same channels from its own
buffers and interrupts.  They must never run at once.  This adds two
globals to audio.c, which is linked before paula.o:

  paula_busy   set by paulaopen(); audioopen() then fails with EBUSY
  audio_nopen  /dev/audio clients now open; paulaopen() fails with EBUSY
               while it is not zero

The count goes up where a client joins the client list in audioopen() and
down where it leaves the list in audioclose().  Both run in process
context on a uniprocessor kernel, so a plain int is enough.

/dev/audio (c 46,0) is one STREAMS device, not a clone: every process
that opens it shares one stream.  STREAMS calls audioopen() on every
open() of that stream but audioclose() only on its last close, and the
stock audioopen() gives each call a new client and points the stream at
it.  A second opener therefore took the first one's stream over, and
once counted here, it was never counted down: audio_nopen stuck at 1 and
/dev/paula refused everyone until a reboot (2026-10-27, a test probe
opening /dev/audio while mikmod played).  So audioopen() now refuses a
stream that is already open (EBUSY): one client at a time, which is all
one stream can carry, and the count stays exact.

The source is Commodore's and is not in this repository, and neither is
any of its text: every place is found by a fingerprint (linepatch.py)
and only our own lines go in after it.
"""
import sys

from linepatch import apply, read, write

# (what, lines, sha256 of those lines, at, lines deleted there, lines inserted)
PLACES = [
    ('audio_clients declaration', 1, '72aeadf245332892dcf256b8d1a10f73e4c795fe7207431a596dd1578e1d1bef', 1, 0, [
        "int paula_busy;\t\t/* /dev/paula holds the channels (src/paula.c) */",
        "int audio_nopen;\t/* clients on the list: /dev/paula waits for 0 */",
    ]),
    ('audioopen MODOPEN test', 2, '50f82981f433b6f645666ff58c60ed12b0ad392a13fe55d188792c1e55c8eafc', 2, 0, [
        "    if (paula_busy)",
        "\treturn EBUSY;",
        "    if (q->q_ptr != NULL)\t/* the one stream is someone's already */",
        "\treturn EBUSY;",
    ]),
    ('audioopen list insert', 1, '6d4aa0e4693fbd2dc350b1a03f9053e937b5b6a6acf6ca2637af6d769cd3d446', 1, 0, [
        "    audio_nopen++;",
    ]),
    ('audioclose unlink', 1, '5554e66748e994cd1b160eed8812023c8b35b3777bc42cc95a327430583a823e', 1, 0, [
        "    audio_nopen--;",
    ]),
]

path = sys.argv[1]
lines = read(path)
if any("paula_busy" in l for l in lines):
    sys.exit("[FAIL] audio.c already carries the /dev/paula exclusion")
write(path, apply("audio.c", lines, PLACES))
print("      [ok] audio.c: paula_busy / audio_nopen exclusion with /dev/paula")
