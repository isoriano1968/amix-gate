# METAL-PROBE-RUN.md — zz9k_memprobe on the A4000D (2026-09-16), run sheet for the Z3660 firmware side

> **Status (2026-09-26).** Run and PASSED on 2026-09-16 (`docs/DEMO-RTG.md`, DESIGN §11.5); kept as the historical run sheet.
>
> **Out-of-repo citations.** Evidence directories are cited by their date-stamped names. They, sibling repositories and archived images are kept outside this repo and are not needed to build or use the driver.

Ground rules: the card in the machine may be used (it is rewritten
the next day); no firmware flash, no card-file changes, no kernel; clean halt, preset 7 selected
at the end. The Z3660 firmware side operates; the driver side directs and verifies.

**Binary:** `2026-09-16-z3660-rtg-metal/zz9k_memprobe` (kept outside the repo)
(sha256 `f92d016582d58a4e…`; cross-built with the wrapper, explicit `-o`; dynamically linked
against `/usr/lib/libc.so.1`; 68020 code; no floating point). Source: `tools/zz9k_memprobe.c`
(commit dc6565b), a copy beside the binary. Transfer to the box as root (e.g. `/tmp/zz9k_memprobe`,
`chmod 755`). It maps only the register page (4 KiB at `0x10000000`) and an 8 MB framebuffer
slice at `0x10200000` through `/dev/mem` — never the 128 MB window — after raising its own VM
limit; every access is announced before it is made and faults are caught and printed.

**Before the run**, capture on telnet: `uname -a`, `ulimit -a`, `ls -l /dev/mem`; on the ARM
serial the boot's `[Core1] Autoconfig RTG to 0x1000` line (= the `autoconfig_rtg NO` hard map).

**Run 1**, as root, to a file and the screen:
```
/tmp/zz9k_memprobe 30 5 2>&1 | tee /tmp/probe1.txt
```
Expected, in order:
```
SELFTEST ... AGREE                              /dev/mem mapping lands on physical
READ32 FW_VERSION 0x1a0 = 00000103 / PRESENT    the signature; if not present it stops
READ16 ... 0x1a0 = 0000 / 0x1a2 = 0103          A1: lanes of 16-bit reads
WRITE32 ... MODE = 00000105                     1920x1080 565: the HDMI switches mode
longword store/readback MATCH; row N via lseek+read = f800f800; row 0 = 00000000
FRAME 1 NOW: bars 1920x1080 -- holding 30 s     -> HDMI frame 1 during these 30 s
FRAME 2 NOW: blue -- holding 30 s               -> frame 2
FRAME 3 NOW: bars again -- leaving them          -> frame 3 (the final state)
zz9k_memprobe: done
```
A line `PROBE FAULT …` means stop and send the whole file (its last line names the access).
The picture stays on the HDMI afterwards: the Z3660 has no switch back — expected.

**Run 2** (only if run 1 completed) — the 1280x800 mode, 20 s holds, one frame during FRAME 1:
```
/tmp/zz9k_memprobe 20 17 2>&1 | tee /tmp/probe2.txt
```

**Send back:** `probe1.txt` (and `probe2.txt`), the frames at full resolution, the serial log
around the runs (any "write unaligned" or RTG debug lines), the telnet captures above. Then a
clean halt, preset 7 left selected.

**What the run answers:** A1 (16-bit lanes and byte order on the FPGA path — the READ16 pair,
the `f800f800` row readback and the bar colours in the frame), A6 (the monitor takes 1920x1080
and 1280x800 from the Z3660), Q6/A2 (what the HDMI shows before and after), and the whole
"real-060 path" question: hard map present, 32-bit register file, framebuffer at +0x200000,
HDMI output — without a driver or a kernel.
