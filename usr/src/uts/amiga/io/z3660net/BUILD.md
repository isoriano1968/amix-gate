# Building & integrating z3660eth into the AMIX kernel

The driver is a **STREAMS/DLPI network driver**, so it does **not** use the SCSI
`build-kernel.sh` (which generates `scsicard[]` rows into `sd.c`). It needs three
kernel-integration edits + its own driver subtree. `../amix-kerntools/build-net-kernel.sh`
automates everything below; this file is the authoritative manual procedure and the
fallback if the automation hits anything unexpected on the live tree.

The build/clean-gate runs **on a networked AMIX box** (the WinUAE/Amiberry build box
at `192.168.2.38`). The real A4000+Z3660 has no network — the clean `relocunix` is
shuttled to it via `transfer.hdf` (the two-tier routine is spelled out in §7).

## 0. Prereqs
- Build box up: `amiberry --config ../amix-kerntools/configs/amix-golden.uae` + the
  grimoire `amix-lan-up.sh` bridge. Verify: `python3 ../amix-kerntools/tools/amixsync.py ls /usr/sys`.
- Tools: `../amix-kerntools/tools/amixsync.py` (FTP push/pull), `../grimoire-amix/tools/host-net/amixsh.py` (run commands).

## 1. Push the driver subtree
Create `/usr/sys/amiga/driver/z3660eth/` and push `src/z3660eth.c`, `src/z3660eth.h`,
`src/z3660ethuser.h`, `src/Makefile` into it.
```
amixsh.py "mkdir /usr/sys/amiga/driver/z3660eth"
amixsync.py push src/z3660eth.c       /usr/sys/amiga/driver/z3660eth/z3660eth.c
amixsync.py push src/z3660eth.h       /usr/sys/amiga/driver/z3660eth/z3660eth.h
amixsync.py push src/z3660ethuser.h   /usr/sys/amiga/driver/z3660eth/z3660ethuser.h
amixsync.py push src/Makefile         /usr/sys/amiga/driver/z3660eth/Makefile
```

## 2. Edit `/usr/sys/master.d/kernel.c`  (pull, edit, push — keep a backup)
**(a)** Add the streamtab extern, right after the stock `aen` one:
```
extern struct streamtab aeninfo;
extern struct streamtab z3660ethinfo;     /* <-- add */
```
**(b)** Claim the `cdevsw[]` slot. `z3660eth` takes major **51** — VERIFY it is still
the empty `/*51*/` row (`…,notty,nostr,nullflag,`) before writing, then replace it:
```
/* was: */ ND,ND,ND,ND,ND,ND,ND,ND,ND,ND,notty,nostr,nullflag,         /*51*/
/* now: */ ND,ND,ND,ND,ND,ND,ND,ND,ND,ND,notty,&z3660ethinfo,nullflag,  /*51=z3660eth*/
```
> **Why major 51** (renumbered from 48 on 2026-07-30). 51 is the **lowest slot of the
> ecosystem-verified-free set 51–67 and 69**. The original 48 was picked in 2026-06 simply
> because it was an empty `nostr` row on the stock/a4091 kernel — but it turns out to be
> **contested**: `zz9000-amix`'s `integration/kernel.c.txt` documents an unpublished
> development tree using *"47 for Hydra, **48 for random**, 49 for ZZ9000, and 50 for sad"*.
> The surrounding claims: **49** = `zz9000` (published), **68** = `va2000`, **47** = a double
> claim by hydra *and* the stock `contrib/mx` MAX-125 driver (the ecosystem's oldest
> collision, 1992). Slots **7, 38, 39, 44, 45** are traps (reserved markers, or
> `KERNEL_DEBUGGER`-only), and **≥ 70 does not exist** — `CDEVSIZE` is 70, so a higher major
> needs the array extended. That leaves 51–67 and 69; we take the lowest.
> Convention and full cross-tree evidence: **`../grimoire-amix/docs/reference/major-number-registry.md`**
> (the Major-Number Registry — "take 51–67 or 69; respect an existing community claim, even a
> weak one"). Renumbering was cheap for us and free for them: only the `/dev/zen0` node and the
> cdevsw row move. **48 is vacated — do not reuse it.**
> No `int2_tbl[]` / `init_tbl[]` edit. The driver uses **lazy-open** (autoconfig on
> first `open`, like `aen`) and **polled RX** (a `timeout()` callout, since AMIX has no
> level-6 hook for the firmware's INT6). This is what makes the GEM-less build box boot
> cleanly and `open()` simply return ENXIO. (Interrupt-driven RX — adding `z3660ethintr`
> to `int2_tbl` — is a later step gated on a board-mod; see scoping §10.2.)

## 3. Edit `/usr/sys/amiga/driver/Makefile`
Add `z3660eth/exp` to the `OBJ` list (after the stock `aen/exp`) and add its build rule:
```
OBJ = \
        ... \
        aen/exp \
        z3660eth/exp \          # <-- add (before/after hydra/exp if present)
        ...

z3660eth/exp :                  # <-- add this rule
        cd z3660eth; $(MAKE)
```

## 3b. The two on-box patch scripts — outcomes, and how to test them without a box
Steps 2 and 3 are what `tools/patch-kernel.sh` and `tools/patch-makefile.sh` do on the box
(pushed and run by `../amix-kerntools/build-net-kernel.sh` / `build-combined-kernel.sh` /
`tools/onbox-build.sh`). Both are pre-POSIX-`/bin/sh` + SVR4-`ed` only, both are idempotent,
and since 2026-07-31 both are **fail-closed**: each edit is a separately guarded arm that is
**post-verified in the file**, because SVR4 `ed` reports an address-search miss with a bare
`?`, keeps reading the here-document as commands, writes the file anyway, and exits 0. Read
the outcome from the exit status, not from the fact that the script ran:

| rc | first line | meaning |
|----|------------|---------|
| 0 | `… ALREADY PATCHED` | every arm was already present; nothing was written |
| 0 | `… PATCHED:` | every arm that was needed landed **and** was verified in the file |
| 4 | `… REFUSED (<verdict>)` | pre-flight said no — **the file was not touched** |
| 5 | `… HALF-PATCHED` | an arm did not land; the named arm failed, restore `<file>.bnkorig`, do **not** relink |

`patch-kernel.sh`'s pre-flight is **row-scoped** and shares kerntools `tools/cdevsw-gate.sh`'s
verdict vocabulary — `FREE`/`OURS` proceed; `MOVED` (our streamtab claims a row that is not
51 — a tree integrated **before** the 48→51 renumber), `TAKEN`, `MISSING`, `AMBIGUOUS`,
`NOANCHOR`, `NOKERNELC` refuse. `MOVED` prints the migration steps: vacate the old row back
to the `nostr` filler, keep the extern, re-run. A name-wide `grep z3660eth` guard used to
call that state ALREADY PATCHED and leave slot 51 unclaimed while renumbered `/dev/zen0`
nodes opened 51 — silent no-ethernet.

Both scripts take an optional path argument (default the on-box path), which is what lets
the whole thing be tested host-side with no box, no emulator and no build:
```
bash tools/patch-selftest.sh      # ~20 fixture scenarios, exits non-zero on the first failure
```
It runs the battery under every plain shell on the host (`sh`, `dash`), lints both scripts for
bashisms (`$(…)`, `[[`, `local`, arrays, `printf`, `grep -q/-E`, `sed -i/-E`), drives the
post-verify with an `ed` shim that deliberately drops an arm, and checks the replacement
cdevsw row is still the single column-0 line that
`../amix-kerntools/tools/cdevsw-gate-selftest.sh` lifts out of `patch-kernel.sh`. **That row
text and its `/*51=z3660eth*/` tag are a cross-repo contract — do not reshape them.**

## 4. Clean-gate relink (the D245 boot-breaker workaround)
On-box `ld` corrupts ~70% of kernel links under emulation, so relink until the kernel is
provably clean. For this (larger) net kernel the bar is **`nm -h -u unix` empty +
`checkunix`-clean** — `sum -r` recurrence does *not* converge at this size — the bar is
implemented by `../amix-kerntools/tools/build-clean-net-kernel.sh`, whose header states it.
The net-aware gate removes the right stale objects (the SCSI gate is hardcoded to
`alien/` and will NOT rebuild a `driver/` subdir or `kernel.c`):
```
amixsync.py push ../amix-kerntools/tools/build-clean-net-kernel.sh /tmp/build-clean-net-kernel.sh
amixsh.py "nohup sh /tmp/build-clean-net-kernel.sh z3660eth > /tmp/bcnk.log 2>&1 &"
# poll /tmp/bcnk.log for 'STABLE after N builds' (clean) or 'FAILED'
```
The gate force-removes `amiga/driver/z3660eth/{z3660eth.o,exp}`, `amiga/driver/exp`,
`master.d/kernel.o`, `master.d/exp` each round (Makefiles have no header-dep tracking),
recompiles, links, and confirms a clean `relocunix` via **`nm -h -u unix` (empty) + `checkunix`**.

## 5. Install + boot
```
amixsh.py "cd /usr/sys; make install"   # stages relocunix -> /stand, keeps OLDunix
amixsh.py "cd /stand; make"              # writes the boot partition (c6d0s3)
amixsh.py "shutdown -i6 -g0 -y"          # reboot
```

## 6. Phase-1 gate (build box) — expected result
The box boots normally; `z3660eth` is registered at cdevsw major 51 but finds no
Z3660 GEM (the build box emulates an a2065). `mknod /dev/zen0 c 51 0; cat </dev/zen0`
(or `slink addaen /dev/zen0 zen0`) should yield **ENXIO**, and the kernel must NOT
panic. That proves the whole integration + build path independent of the datapath.

## 7. Real HW (Phases 2–4) — shuttle, don't re-image
Shuttle the clean `relocunix` to the real box via `transfer.hdf` (`c5d0`), stage it,
`cd /stand; make`, reboot. Then bring the interface up at **192.168.2.39** per
`userland/bringup.sh` and test TX → RX → ARP+ping. Never re-image the root.

## 8. Cache maintenance and the two mapping arms (030 vs 040/060)
Relevant if you are building `z3660eth` into anything other than a stock 68030 AMIX kernel — the
68040/68060 line (`amix-040-060-port`) in particular. Authority: `docs/BLIZZARD-F3.md`.

**The 030 line ships with no cache maintenance and needs none.** Metal bring-up (2026-06-21) ran
TX and RX byte-correct with no flush at all; the eth DDR windows are cacheable there, but the 030
data cache is effectively off in every validated config. Nothing to configure.

**The 040/060 line has line-granular maintenance compiled in, and disabled.** Since 2026-08-24
there is no `Z3660ETH_CACHE_FLUSH` `#ifdef`: the push/invalidate paths are always compiled and
gated at run time by the tunable `long z3660eth_cache` — **0 = off, the shipping default**, `40` =
68040 data cache, `60` = 68060. Arming it is a `/dev/kmem` poke, not a build flag, so the same
`relocunix` serves both lines. Read back through `/dev/kmem` after attach:

| symbol | meaning |
|--------|---------|
| `z3660eth_ci_ok` | 0 = not checked, 1 = the free ride verified, 2 = **FAILED** |
| `z3660eth_maint_on` | 1 = all four conjuncts hold, line ops actually armed |
| `z3660eth_sptalloc_unsafe` | non-zero = the sptalloc arm was taken with a live data cache |
| `z3660eth_dtt0` / `z3660eth_dtt1` / `z3660eth_cacr` | the control registers captured at attach |
| `z3660eth_push_n` / `z3660eth_inv_n` / `z3660eth_push_bytes` / `z3660eth_inv_bytes` / `z3660eth_sync_n` / `z3660eth_range_ovf` | the six line-op counters |

`z3660eth_ci_enforce` (default 0) decides what a failed check costs: 0 warns via `cmn_err` and
continues, non-zero refuses the attach with `ENXIO`.

**The two mapping arms are not interchangeable here.** `z3660eth_map()` takes the physical base as
the kernel VA directly when the whole window lies below `VSECT1` (`0x40000000`) — AMIX's section-0
early-termination descriptor makes `phystokv()` the identity — and only a board at or above
`VSECT1` takes `sptalloc()`. The 040/060 maintenance depends on the direct arm: `CPUSHL`/`CINVL`
select a line by *physical* address, and the `DTT0 = 0x003fc060` free ride that keeps the window
cache-inhibited is what makes the driver's kernel VA a physical one. On the `sptalloc` arm the VA
has no relation to the board's physical address, so **maintenance disarms rather than guessing**
and the arm is declared unsupported with a live data cache. The shipped Z3660 config (fixed base
`0x10000000`) is always the direct arm.

## Real-world build notes (historical record, validated 2026-06-21 on the WinUAE build box)
The current build harness is **Amiberry** with `../amix-kerntools/configs/amix-golden.uae` (§0).
The WinUAE specifics below are kept as the record of where the FTP-asymmetry and clean-gate
lessons were learned; `AmigaUnix-z3660.uae` is that box's own config, not a kerntools one.
The box is **WinUAE on Xvfb :101**, config `AmigaUnix-z3660.uae` (a2065=tap:tap0, guest **192.168.2.38**), root disk
`Amix-z3660-work.hdf` = the build tree. Launch: `sudo /usr/local/sbin/amix-lan-up.sh` + `DISPLAY=:101 winuae -config=…`.
Set `AMIX_PASS` to the build-image root password → `export AMIX_HOST=192.168.2.38 AMIX_USER=root AMIX_PASS=<pw>`.

- **★ FTP asymmetry:** `amixsync.py` **push (PUT) works for 30 KB+**, but **pull (GET) is flaky >~1 KB** (the a2065
  drops large GETs); `amixsh.py` (telnet) only returns small command output reliably. So: push the driver fine, but
  patch `kernel.c` **on the box with `ed`** (`tools/patch-kernel.sh`, no pull) and patch the small `driver/Makefile`
  locally + push. **NFS is the reliable channel** (host `/Amix` = `nas:/Amix`, nas=192.168.2.4; the guest mounts it
  with `mount -F nfs 192.168.2.4:/Amix /mnt`, **nfsvers=2**) — use it to pull the clean `relocunix` and large files.
- **★ rico.h-isms:** a STREAMS driver must spell out `unsigned char/long/short` — **NOT** `uchar`/`ulong`/`ushort`
  (those are `rico.h`-only, which the SCSI driver includes but this one shouldn't). Don't re-declare `untimeout`
  (it's in `systm.h`). Always **single-compile** (`cd …/z3660eth; make`) to catch compile errors before the slow gate.
- **★ clean-gate reality:** on this (larger) kernel the D245 `ld` corruption rate is high, so relink until clean. The
  correct accept bar is **`nm -h -u unix` empty (no undefined symbols) AND `checkunix`-clean** — `sum -r` recurrence does
  *not* converge at this kernel size and is **not** the gate here (it still works for the small SCSI kernels). `checkunix`
  alone is insufficient (it only validates `.symtab`; a code/data shift passes it).
  `../amix-kerntools/tools/build-clean-net-kernel.sh` implements the `nm -u` bar; run it at a high cap.
