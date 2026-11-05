"""linepatch.py -- edit a build-time copy of a stock AMIX source file without
carrying any of its text.  Used by patch_audio_close.py and
patch_audio_stereo.py (imported from the script's own directory).

The AMIX sources are Commodore's.  None of their lines is in this
repository: a place in the file is found by a fingerprint -- a number of
lines and the SHA-256 of those lines exactly as the input has them -- which
must match exactly once.  Each place is

    (what, n, sha256, at, delete, new)

the window of n lines with that digest; at line `at` of the window,
`delete` lines (inside the window, so they are asserted too) go and the
lines of `new` come in.  An entry of `new` is one of our own lines (a
string, without its newline) or COPY(k, sha256): k lines of the input
with that digest, taken from the input as it was before any edit (a stock
line that our code uses where it moves or repeats it).  Every place is
located first, then the edits are made from the bottom up, so no edit
moves another's position.  Lines end at newlines only: the stock files
have form feeds between functions, which str.splitlines() would also
split at.
"""
import hashlib
import sys


def COPY(n, digest):
    return ("copy", n, digest)


def read(path):
    t = open(path, encoding="latin-1", newline="").read().split("\n")
    return [l + "\n" for l in t[:-1]] + ([t[-1]] if t[-1] else [])


def write(path, lines):
    open(path, "w", encoding="latin-1", newline="").write("".join(lines))


def _digest(lines):
    return hashlib.sha256("".join(lines).encode("latin-1")).hexdigest()


def find(lines, n, digest):
    return [i for i in range(len(lines) - n + 1) if _digest(lines[i:i + n]) == digest]


def apply(name, lines, places):
    """lines (a list from read()) edited per places; exits on any mismatch"""
    plan = []
    for k, (what, n, digest, at, delete, new) in enumerate(places):
        hits = find(lines, n, digest)
        if len(hits) != 1:
            sys.exit("[FAIL] %s: %s: fingerprint found %d times, expected 1 "
                     "(not the expected input?)" % (name, what, len(hits)))
        if not 0 <= at <= n or at + delete > n:
            sys.exit("[FAIL] %s: %s: edit outside its window" % (name, what))
        out = []
        for item in new:
            if isinstance(item, tuple):
                src = find(lines, item[1], item[2])
                if not src:
                    sys.exit("[FAIL] %s: %s: copied lines not found" % (name, what))
                out += lines[src[0]:src[0] + item[1]]
            else:
                out.append(item + "\n")
        plan.append((hits[0] + at, k, delete, out, what))
    plan.sort(reverse=True)
    end = len(lines) + 1
    for pos, _, delete, out, what in plan:
        if pos + delete > end:
            sys.exit("[FAIL] %s: %s: overlaps the next edit" % (name, what))
        end = pos
    for pos, _, delete, out, _ in plan:
        lines[pos:pos + delete] = out
    return lines
