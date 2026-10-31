# BLIZZARD F3 round 2 — z3660eth against a real 68040/68060 data cache

**Pre-registration, written 2026-08-24, BEFORE any code in this round. Nothing below is to be
rewritten to match a later measurement.** Round 2 covers **F3-M0** (assert the free ride) and
**F3-M2** (repair the cache block, run it under a gate) for the ethernet driver only. Round 1 —
`../amix-z3660scsi/docs/BLIZZARD-F3.md` — did the same for the SCSI driver and is the template
this round follows; where this round departs from it, the departure is derived and marked.

Confidence tags follow the campaign convention: **measured** (a line of source read, or a number
off a run, with its file), **derived** (arithmetic or logic over measured facts, shown), and
**assumed** (neither — each one below names the gate that settles it).

> **ROUND-2 RESULT (2026-08-24, appended — nothing above was edited to match it).**
> M0 and M2 implemented and landed. Cross-compiled with the real AMIX toolchain (gcc 2.7.2.3 /
> GNU as 2.8.1, the shipped `AMIX_KERNEL_CFLAGS`): **`nm -u` is byte-for-byte identical to the
> pre-change baseline** — the diagnostics use `cmn_err`, which this driver already referenced, so
> nothing new is undefined and the repo's oldest gate does not move. All **six** 040/060
> instruction sites (`movec` ×3, `cpushl`+`cinvl`, `cinvl`, `nop`) are proven dominated by a
> `tstl z3660eth_cache` / `beq` in the disassembly, the three `movec` leaves being reached by
> exactly three PC-relative `jsr`s, all inside the gated arm of `z3660eth_ci_check`. The
> comment-only correction commit is proven **byte-identical** at the object level (`z3660eth.o`
> and `ld -r exp`, sha256 `736afcc2…`). `-Wall` output is unchanged from baseline. **This repo
> has no host harness**, so those object-level proofs are what stands in its place (§6).
> **Nothing here is evidence about a real cache** — §7 is still owed, on metal.

---

## 0. Why this driver has never issued a cache instruction

Every deployment of `z3660eth` that has ever existed has run on an **emulated** CPU — the Z3660
carries the guest 68030/68040 on core1's UAE interpreter. An interpreter has no data cache, so
the flush this driver never issues costs nothing and the stale line that never exists corrupts
nothing.

The driver does contain a cache block, and it has never been built. `src/Makefile:7` is
`CFLAGS = -O -D_KERNEL -DSVR40 -DSVR4 …` with no `-DZ3660ETH_CACHE_FLUSH` (**measured**), and
none is injected by the relink scripts. The repo says so in its own words: metal bring-up needed
*"no cache maintenance … either"* (`README.md` §"Status", **measured**), and the flush helper
was *"Untested, because it was never needed"* (internal handoff `GRIMOIRE-HANDOFF.md:264` —
gitignored, not part of this repo).

So the block is not a working feature that is switched off. It is **four defects in a dead
branch**, and BLIZZARD §G/D8 rules that it is repaired rather than deleted, because the device it
protects fails silently.

---

## 1. THE COVERAGE TRUTH FOR THIS DRIVER

Round 1's central finding was that `DTT0` matches **logical** addresses, so the SCSI driver's free
ride covers its own accesses and **not** the user-page aliases of the buffers it hands the
firmware. That finding is re-verified and holds. **Its consequence for the ethernet driver is the
opposite one, and that is this round's central result.**

### 1.1 What this driver actually shares, and with whom

`z3660eth` never touches AMIX RAM on behalf of anyone. It has no `vtop`, no user buffer, no
scatter list. Every byte it shares with the firmware lives in **board** memory reached through the
Zorro III combo window. Four memory classes, exhaustively (**measured**, `src/z3660eth.c`):

| class | where | aliases that exist | cache mode today (040/060 line) |
|---|---|---|---|
| register file `0x104..0x1A8` | `board_base + 0` | one: the low identity VA (direct arm) | **CI** via `DTT0` |
| RX backlog ring, 64 × 2048 B | `board_base + 0x07ED0000` | one: the low identity VA | **CI** via `DTT0` |
| TX staging frame | `board_base + 0x07EE0000` | one: the low identity VA | **CI** via `DTT0` |
| `tmp[]` / `rxbuff[]` staging | kernel BSS | one: the kernel's own low VA | CI via `DTT0` today |
| STREAMS `mblk` payloads | kernel arena `≥ 0x40000000` | one: the arena KVA | leaf PTE `CM` (copyback) |

The column that decides everything is the third one. **No physical page this driver touches has
two virtual aliases.** The frame windows are device memory with exactly one mapping; the staging
buffers and the mblks are ordinary kernel memory the device never sees. There is therefore no
CPU↔CPU aliasing class here at all — no S5/S6, no S8, no page-cross law, because there is no
`vtop`'d buffer to cross a page.

### 1.2 The inversion: the 030 line is the cacheable one

| kernel line | mechanism reaching `0x1xxxxxxx` | cache mode of the eth windows |
|---|---|---|
| **030 (shipping today)** | supervisor root table section-0 **early-termination** descriptor, page address 0, CI **clear** — TT is *off*, `copyit.s:87-88` pmoves zero into `%tt0/%tt1` (**measured**, `docs/ETHERNET-SCOPING.md:433`) | **CACHEABLE** |
| **040/060 (F4's target)** | `DTT0 = 0x003fc060`, `CM = 11` (**measured**, `amix-040-060-port/src/pstart040.s:326-327`) | **CACHE-INHIBITED** |

The driver's own file header has this backwards, and so does the handoff: both argue from *"the
window is cacheable"* to *"we got away with it because the 030 data cache is effectively off"*
(**measured** — the header's *"the 030 data cache is effectively off in every validated config"*,
`src/z3660eth.c:46-47`; the handoff argues the same at `GRIMOIRE-HANDOFF.md:256-260`, which is
gitignored and not part of this repo). That reasoning is correct **for the 030** and is the
weaker of the two positions. On the 040/060 line the same window is cache-inhibited by a kernel
constant no code in this repo names.

> **So this driver's worst cache exposure is on the machine it already ships on, not on the one it
> is about to meet.** F4 does not introduce the exposure for `z3660eth`; it removes it, for as long
> as `DTT0` stays broad.

### 1.3 What the free ride still does not cover

Three things, and only three:

1. **Ordering.** `CM = 11` is cache-inhibited **NONSERIALIZED**. Cache-inhibition says nothing
   about when a store reaches the device or whether a following load may pass it. This is the one
   hazard that is live on the shipped arm, and it is armed by a knob the port lane has already
   scheduled (§1.4). See N5 in §3.
2. **The `sptalloc` arm.** `z3660eth_map()`'s else-branch (`z3660eth.c:238-244`, **measured**) maps
   the board through page tables when the window is at or above `VSECT1` — `autoconfig_rtg YES`.
   Per-map device cache mode is still `hat040.s`'s deferred TODO
   (`amix-040-060-port/docs/CACHES-ON-PLAYBOOK.md:215`, **measured**), so those pages take
   `hat_cm_ram`, which has been **copyback since 2026-07-30** (**measured**, `hat040.s:1851-1869`,
   `B2 0x20`). A cacheable register file is not a coherence problem to be flushed away — it is an
   unusable device (§3.5).
3. **The scheduled end of the free ride.** `DTT0` narrowing is not a vague future: it has a
   contract. `amix-040-060-port/docs/contracts/DTT0-NARROWING-SPEC.md:169-187` (**measured**)
   specifies stage **N1** as a *low 16 MiB* NC compatibility window — `0x0000c060`. The Z3660 combo
   window is at **`0x10000000` = 256 MiB** (**derived**), far outside it. After N1 this driver's
   windows are covered only if the new low identity map classifies them explicitly; the spec's own
   classifier table says *"custom/CIA/Ramsey/controller MMIO → NCS/NC; never ordinary RAM CM"* and
   names *"chip-RAM Paula buffers and board apertures"* among the classes that break without it
   (`:206-214`, `:155`, **measured**). Whether a 128 KB DDR-backed frame window at
   `board+0x07ED0000` is classified as device MMIO or mistaken for RAM is **a decision in another
   repo that this driver cannot see** — which is precisely why F3-M0 asserts rather than assumes.

### 1.4 What the 060-D knobs change

Only one row matters here (**measured**, `amix-040-060-port/docs/060-D-CACHE-KNOBS-PLAN.md:26-33`):
the data cache is **already on and copyback**, so nothing about it is pending; the **store buffer**
(`CACR` bit 29) is **off**, candidate 1, and turning it on is what arms N5. Neither knob touches
`DTT0`, and neither makes the board window cacheable.

---

## 2. THE ORDERING DECISION, AND THE PARADOX IT HAS TO ANSWER

**Decided: the repair lands NOW, default-off, poke-armed.** Same as round 1, for one shared reason
and one that is specific to this driver.

Shared: BLIZZARD §9 gates F4 on F3 because a first real-silicon boot with a driver full of unfixed
cache defects produces corruption indistinguishable from a kernel bug, and burns the one clean
first boot on an ambiguity.

Specific, and it must be said plainly rather than buried: **on the shipped direct-map arm the
maintenance this round adds is redundant with the free ride.** Every op it issues acts on a line
that cannot exist, because the window is cache-inhibited. Three reasons it lands anyway:

1. **The choreography is the deliverable, not the instruction count.** §1.3.3 gives the free ride a
   scheduled expiry in another repo. The day the low-map owner classifies this window wrong, the
   difference between a poke and a driver redesign is whether the correct RX/TX order was worked
   out, reviewed and compiled in before it was needed.
2. **A counter that moves is what makes the metal A/B interpretable.** A green run that proves
   nothing ran is a failed run (round 1's rule, kept).
3. **One part of it is not redundant at all.** `z3660eth_sync()` — the serializing barrier — is a
   hazard on the CI arm itself, because CI-nonserialized is exactly the mode in which a load may
   pass a buffered store (§3.6).

"Default-off" is what makes "now" safe; §4.

---

## 3. SUSPECT DISPOSITION

Every N-item from `Amix/tmp/2026-08-24-lc060-recon/RECON.md`, re-verified in-tree today against
`src/z3660eth.c` and `src/z3660eth.h` as they stand at `fc35b8f`.

| # | claim | re-verified? | disposition |
|---|---|---|---|
| **N1** | `sptalloc` arm: *"this kernel has no `PG_CI` bit at all"* is an 030 fact restated as universal | **yes** — `z3660eth.c:30-38`, and `:198` *"Cache semantics are identical either way"* | **COMMENT FIXED + ARM DECLARED UNSUPPORTED (M0)** — §3.4 |
| **N2** | polling a device-written **register** (`ZZ_ETH_RX_ADDR`) with no invalidate | **yes** — `z3660eth.c:595` | **NOT A CACHE PROBLEM — an arm problem.** §3.5 |
| **N3** | polling the device-written **4-byte slot header** with no correct invalidate | **yes** — `z3660eth.c:605-610` | **FIXED (M2)**: real `cinvl` before the header read, every iteration — §5.2 |
| **N5** | publish→trigger with no barrier, and `PUSH` is a no-op | **yes** — `z3660eth.c:547-552`; `PUSH` expands to nothing (`:152`) | **FIXED (M2)**: real push + gated barrier; the load-after-store is the real unordered pair — §3.6 |
| **N6** | payload at `slot+4` is structurally 16-byte-misaligned ⇒ partial-line bug | **yes** — `ZZ_RX_FRAME_PAD 4` (`z3660eth.h:43`); ring slots are 2048-aligned so `slot+4 ≡ 4 (mod 16)` | **FIXED (M2), and the safety argument is derived, not inherited** — §3.2 |
| **N7** | `Z3660ETH_PUSH` and `Z3660ETH_INVAL` are the same function (both `cpushl`) | **yes** — `z3660eth.c:149-150` | **FIXED (M2)**: two functions, and the way it actually bites is not the way the recon says — §3.1 |
| **N8** | `CPUSHL`/`CINVL` take **physical** addresses; the code passes a kernel VA | **yes** — `z3660eth.c:143-147` operates on `info->frame`/`info->txwin` | **FIXED (M0+M2)**: the identity is asserted, and the assertion **gates the ops** — §3.3 |
| — | the whole block is `#ifdef Z3660ETH_CACHE_FLUSH` with no `-D` anywhere | **yes** — `src/Makefile:7` | **`#ifdef` DELETED**: compiled in always, gated at run time — §4 |

### 3.1 N7 — two functions, and the failure that actually bites

The recon's statement of the hazard is right in general: `CPUSHL` writes a dirty line back, so
using it where an invalidate was meant writes stale CPU bytes over fresh device bytes. **In this
driver that specific failure cannot fire**, and the reason is worth writing down because it is the
same reason N6 is safe: the RX ring is **device-write-only** — this driver reads slots and strobes
a register, and never stores into the ring (**measured**, `z3660eth.c:594-639`). No line covering
the ring can be dirty, so a push over the ring degenerates to an invalidate.

The failure that *does* bite is the 68060's: **`CPUSHL`'s invalidation is conditional on `CACR.DPI`
— clear pushes and invalidates, set pushes and leaves the line VALID**
(**measured**, `amix-040-060-port/docs/contracts/A3091-B2-PREPARE-PATCH-SPEC.md:334-336`). With
`DPI` set, a push-as-invalidate **does not invalidate**, the next drain reads the same line, and the
driver re-delivers a stale frame or misses a serial. Silent, and exactly the failure mode BLIZZARD
§6.3 says makes deletion the wrong answer for this device.

So: `PUSH` and `INVAL` become two functions with different opcodes, and every push is followed by
an explicit `CINVL` (recorded as *"harmless"* on the 040 by the same contract), which makes one
unconditional sequence correct on both parts with no CPU-class branch. `CACR` is captured at attach
so a set `DPI` is visible rather than inferred.

### 3.2 N6 — the partial-line derivation

`ZZ_RX_FRAME_PAD` is 4 and the ring stride is 2048, so the payload starts 4 bytes into a 16-byte
line at every slot (**derived**, and the constants are `z3660eth.h:42-43`). Any line-granular
operation over `[slot+4, slot+4+len)` must round outward, and outward rounding touches (a) the
4-byte header in the first line and (b) up to 15 bytes of unused slot padding at the tail.

Round 1's safety argument for outward rounding was *"the same range was pushed before the transfer
and the transaction is synchronous at spl6"*. **That argument is not available here** — RX is
asynchronous by construction (the firmware fills slots at GEM-RX time; the driver drains from a
clock callout). This round derives a different and stronger one:

> **Every byte of the RX ring is written by the firmware and never by this driver. A line covering
> the ring can therefore never be dirty, so an outward-rounded `CINVL` over any ring range can
> discard nothing.**

Both ends of the rounding are then safe for the same reason, and the header is *wanted* in the
first line's invalidate anyway. The TX window is the mirror image: it is **CPU-write-only**, so the
tail rounding there is covered by using `CPUSHL` (write back, then invalidate) rather than a bare
invalidate — the same rule round 1 stated as *push-before-write, never invalidate-after*.

**The caveat, recorded because it is what would silently invalidate the argument:** this holds only
while the ring stays device-write-only and the TX window stays CPU-write-only. A future zero-copy
RX that hands a slot upstream, or any driver-side write into a slot header, kills it. If either
happens, this section must be re-derived, not re-read.

### 3.3 N8 — the identity is asserted, and the assertion gates the ops

`CPUSHL`/`CINVL` select a line **by physical address**. This driver holds kernel VAs, and on the
direct arm those VAs equal the physical address only because section 0 is an identity map —
`phystokv(p)` is `(p)` (**measured**, sysroot `sys/immu.h:348`), and on the 040/060 line the same
`DTT0` that provides the CI also provides the identity, because that is what a transparent
translation *is*.

Round 1 needed no conversion (it holds physical addresses by contract) and could treat the
assertion as reporting. **Here the assertion is load-bearing safety**, because the failure is not
"no maintenance" but "maintenance of the wrong physical line": on the `sptalloc` arm the VA comes
from section 1 and has no relation to the board's physical address, so a `CINVL` on it would
**discard an unrelated dirty line belonging to some other subsystem**. That is a data-destroying
bug introduced by a cache fix — the exact shape of thing this campaign exists to not do.

Therefore the arming rule, in full:

```
maintenance armed  ⇔  z3660eth_cache != 0            (the gate: a real 040/060 DC)
                  ∧  z3660eth_direct_map == 1        (not the sptalloc arm)
                  ∧  phystokv() is the identity on both mapped regions   (checked, not assumed)
                  ∧  z3660eth_ci_ok == 1             (a TTR covers both regions, cache-inhibited)
```

The last conjunct is not belt-and-braces: **a TTR match is the only thing that makes a logical
address a physical one here.** If the regions fall to the page tables, VA == PA is no longer
guaranteed and the line ops must not fire. The barrier is separate — it takes no address, so it is
gated on `z3660eth_cache` alone.

**Consequence, stated because it is uncomfortable and true:** the maintenance is armed exactly on
the configuration where it is redundant (§2). The alternative — arming it where the identity is
unproven — is worse than doing nothing.

### 3.4 N1 — the mapping classification, and the arm decision

`z3660eth.c:30-38` says the window *"is cacheable on the 030 … and per-page cache-inhibit is
impossible whichever way the board is mapped: this kernel has no `PG_CI` bit at all
(immu.h:68-75)"*. Re-verified: the sysroot's `sys/immu.h` really does define only
`PG_ADDR/PG_LOCK/PG_M/PG_REF/PG_W/PG_V` (**measured**). The sentence is **true of the 030 kernel and
false of the 040/060 line**, whose leaf PTEs carry a two-bit `CM` at bits 6-5 that the kernel
already writes (`hat_cm_ram`; **measured**, `hat040.s:1851-1869`). `z3660eth.c:198`'s *"Cache
semantics are identical either way"* is false on the 040/060 line for the same reason — and there
it is worse than false, because it is the sentence that makes the two mapping arms look
interchangeable when one of them is unusable.

The two arms, classified:

| arm | when | regs | frame windows | verdict on real silicon |
|---|---|---|---|---|
| **direct** (`z3660eth.c:232-237`) | board below `VSECT1`; the shipped `autoconfig_rtg NO` fixed base `0x10000000` | CI via `DTT0` | CI via `DTT0` | **SUPPORTED.** The free ride, asserted every attach (M0) |
| **sptalloc** (`:238-244`) | board at/above `VSECT1` (`autoconfig_rtg YES`) | leaf `CM` = `hat_cm_ram` = **copyback** | same | **UNSUPPORTED.** Detected, counted, reported |

The free ride must hold for the **direct** arm, and that is what the attach-time assert covers,
both regions, exhaustively (§4).

### 3.5 N2/N3 — a register is not a slow memory

The recon groups `ZZ_ETH_RX_ADDR` (a register) with the slot header (memory) as "polling
device-written values". They need opposite answers.

- **The slot header (N3) is memory** and takes a real invalidate before every read (§5.2).
- **The register (N2) cannot be fixed by cache maintenance at all.** A read of a cacheable MMIO
  location allocates a **16-byte line** — one bus burst covering `0x190..0x19F`, i.e. `ZZ_ETH_TX`,
  `ZZ_ETH_RX`, `ZZ_ETH_MAC_HI` and `ZZ_ETH_MAC_LO` — so touching any one register issues bus reads
  of the other three as a side effect, and a subsequent read of any of them may never reach the bus
  at all. Interleaving `CINVL` before every register read would be a cache op per MMIO access with
  a race in every gap.

That is the whole argument for §3.4's verdict: the `sptalloc` arm is not "unmaintained", it is
**unmaintainable**, and the honest thing is to detect and say so rather than to ship a flush that
implies otherwise.

### 3.6 N5 — publish→trigger, and where the ordering hazard actually is

TX today is: `bcopy` into `txwin`, a `PUSH` that expands to nothing, `WRLONG(ZZ_ETH_TX, pktsize)`,
then `rc = RDLONG(ZZ_ETH_TX)` (**measured**, `z3660eth.c:547-552`).

Under `CM = 11` with the 060 store buffer **on**, the frame stores and the trigger store are all
buffered. The frame-then-trigger order is preserved by a FIFO buffer, so the *publication* order is
not the sharp edge (**derived**; it is `assumed` that the buffer is FIFO and that the firmware's
view is bus order). The sharp edge is the **load immediately after the store, to the same
address**: `rc = RDLONG(ZZ_ETH_TX)` may be satisfied ahead of the buffered trigger and return the
**previous** transaction's TX result — which this driver turns into `packets_sent++` or a
`tx_errors++` for the wrong frame, silently.

The RX side has the mirror pair: `WRLONG(ZZ_ETH_RX, 1)` (advance the slot) followed on the next
iteration by `RDLONG(ZZ_ETH_RX_ADDR)`. A load passing that buffered store reads the *old* cursor;
the driver then sees `serial == last_serial` and breaks out of the drain early. Not corruption —
one poll tick of latency — but it is the same defect and it costs one instruction to close.

Barriers are therefore placed at three points (§5), gated, **now**, before the knob that arms them
lands. **That `nop` serializes pending writes on the 68060 is `assumed`, not measured** — the claim
is owed a read of the MC68060UM before the store-buffer rung, on the same discipline that made
F1-M0 settle PCR bit 1 from the manual before any PCR code shipped.

---

## 4. THE GATE

Identical in shape to round 1, for identical reasons. This file is compiled **once**, with
`-m68020` (**measured**, `AMIX_KERNEL_CFLAGS` in `gcc-cross-amix/build/env.sh`), and the one object
is linked into kernels that run on an emulated 030, an emulated 040, and — from F4 — a real 68060.
There is no compile-time discriminator, and `cputype` may **not** be referenced: the stock kernel
has no such symbol, so an `extern` would break this repo's `nm -u` clean gate on the 030 line.

```c
long z3660eth_cache;        /* 0 = off (shipping default); 40 = 68040 DC; 60 = 68060 DC */
long z3660eth_ci_enforce;   /* 0 = warn on a failed free-ride check; else refuse the attach */
```

Named `z3660eth_*` rather than `zen_*` so it sits in the same namespace as every other
kmem-readable global in this file (`z3660eth_direct_map`, `z3660eth_present`, …) and the existing
`/dev/kmem` + `nlist` recipes need no new prefix.

The `#ifdef Z3660ETH_CACHE_FLUSH` is **deleted**: the code compiles unconditionally and is gated at
run time. A `-D` nobody passes is not a switch, it is a way to keep code from ever being reviewed.

Instructions are emitted as raw `.word`s with the address pinned in `%a0` by an explicit register
variable. **Encodings measured today, in this repo, both directions** — assembled with this
toolchain's `as` and disassembled independently:

| instruction | word(s) | evidence |
|---|---|---|
| `cpushl dc,%a0@` | `f468` | `as -m68040` → `f468`; `objdump -m m68k:68040` → `cpushl dc,%a0@` |
| `cinvl dc,%a0@` | `f448` | same, both directions |
| `nop` | `4e71` | same, both directions |
| `movec %dtt0,%d0` | `4e7a 0006` | `as` round-trip; gcc 2.7.2.3 emits it and `objdump` names it |
| `movec %dtt1,%d0` | `4e7a 0007` | `as` round-trip |
| `movec %cacr,%d0` | `4e7a 0002` | `as` round-trip |

The register-pinning construct was compiled with the real `AMIX_KERNEL_CFLAGS` before being
designed in: `register unsigned char *p __asm__("a0")` genuinely lands the pointer in `%a0`
(`moveal %d1,%a0`, loop increments `%a0`), and every emitted word sits under a `tstl` / `beqw` of
the gate. Types are spelled out — this repo's rule is that a STREAMS driver must not use `rico.h`'s
`uchar`/`ulong`, so round 1's `ulong` signatures are respelled here.

**The free-ride assert is exhaustive, not endpoint-sampled.** Round 1 checked two endpoints on the
argument that a 16 MB TTR granularity means a 0x90000-byte window straddles at most one boundary.
This driver's window spans `0x07EF0000` (**measured**, `z3660eth.h:88`), so that argument does not
carry: a TTR mask with non-adjacent bits covers a **non-contiguous** set of 16 MB blocks, and two
endpoints would not see a hole between them. The check therefore walks the two regions the driver
actually touches — `[base, base+ZZ_REGS_BYTES)` and
`[base+ZZ_RXBACKLOG_OFF, base+ZZ_WINDOW_TOP)` — one probe per 16 MB block, which is exhaustive
because a TTR's match function depends only on address bits 31-24. Three probes on the shipped
geometry.

---

## 5. THE CHOREOGRAPHY

Derived per path from the firmware's own publication order, not copied from round 1.

**The firmware's RX publication order, measured** (`Z3660/…/Z3660/src/ethernet.c:551-560`): the ARM
`memcpy`s the payload to `slot+RX_FRAME_PAD` **first**, then writes the length at `slot+0..1`, then
the serial at `slot+2..3` **last**. The serial is the publication token, and it is published after
the bytes it certifies. The driver's read order — header, then payload — is the matching acquire
order, so the maintenance goes in the same two steps and in the same order.

One further firmware fact worth carrying (`ethernet.c:620-628`, **measured**): when the backlog
wraps, `ethernet_receive_frame()` **writes** `frm[2]`/`frm[3]` of the new current slot at
strobe time. The header is not merely written once at fill time — it can change under the driver at
every `ZZ_ETH_RX` strobe, which is another reason it may never be read from a line that survives a
strobe.

### 5.1 TX (`z3660ethxmit`), per frame

```
1.  bcopy(tmp -> txwin, pktsize)          the frame bytes
2.  push  [txwin, txwin+pktsize)          cpushl+cinvl: every cached alias written back so
                                          the ARM reads current bytes.  Outward tail rounding
                                          is safe because this is a PUSH and the TX window is
                                          CPU-write-only (§3.2)
3.  sync                                  gated barrier: the frame bytes precede the trigger
4.  WRLONG ZZ_ETH_TX = pktsize            trigger (synchronous, ~1 ms)
5.  sync                                  gated barrier: THE one that matters (§3.6) -- the
                                          result load must not pass the trigger store
6.  rc = RDLONG(ZZ_ETH_TX)
```

`tmp[]` is deliberately **not** maintained: it is kernel BSS with exactly one alias, written and
read by the CPU only. Maintaining it would buy nothing and would drag its BSS neighbours into a
rounded range.

### 5.2 RX (`z3660eth_drain`), per ring slot

```
1.  off  = RDLONG(ZZ_ETH_RX_ADDR)         register: fresh on the CI arm, unmaintainable on the
                                          other (§3.5).  Ordered w.r.t. the previous strobe by
                                          step 8
2.  slot = frame + (off - ZZ_RXBACKLOG_OFF)
3.  inval [slot, slot+ZZ_RX_FRAME_PAD)    cinvl -- a REAL invalidate now, not a push (§3.1).
                                          One line; discards nothing (§3.2)
4.  read serial, len                      serial is the firmware's publication token
5.  if serial == last_serial: break       ring drained
6.  inval [slot+PAD, slot+PAD+len)        AFTER the token says the payload is published.
                                          Outward rounding covers the header line (already
                                          consumed) and slot padding (never CPU-written)
7.  bcopy(slot+PAD -> rxbuff, len)
8.  WRLONG ZZ_ETH_RX = 1 ; sync           advance the slot, then the gated barrier so the next
                                          iteration's cursor read cannot pass it (§3.6)
```

The bad-size arm (`len < ETH_HEADER_SIZE || len > MAX_BUFFER_LENGTH`) takes the same strobe and the
same barrier and skips steps 6-7, because it never reads the payload.

`rxbuff[]` and the destination `mblk` are **not** maintained, for §5.1's reason: single alias, CPU
only, never seen by the device.

### 5.3 What is deliberately not maintained, and why that is a decision

| memory | maintained? | why |
|---|---|---|
| `tmp[]`, `rxbuff[]` (kernel BSS) | **no** | one alias, CPU-only. Self-coherent under any cache mode |
| STREAMS `mblk` payloads | **no** | one alias in the kernel arena; the device never touches them |
| register file | **no** | maintenance cannot make MMIO safe (§3.5); the arm decision covers it |
| RX ring, TX window | **yes** | the only CPU↔device memory this driver has |

This table is the reason the round adds four call sites and not forty.

---

## 6. THE SHIPPING-PATH PROOF

The 030 and emulated-040 lines are the shipping deployments and must not move. **This repo has no
host harness** — there is no `test/` directory and the driver is a STREAMS module with no
oracle-able synthesis surface of the kind that made round 1's `make -C test/host check` possible.
What stands in its place is three mechanical proofs against the real toolchain, and the report
records the numbers rather than asserting the property:

1. **`nm -u` is byte-for-byte unchanged.** The pre-change undefined set is captured before the
   first edit; the diagnostics use `cmn_err`, which is already in it, so nothing new is referenced.
   A driver that adds an undefined symbol does not link into the stock kernel — this repo's oldest
   gate.
2. **Comment-only commits are proven byte-identical** at the object level, cross-compiled with the
   real `AMIX_KERNEL_CFLAGS` before and after. Already the house standard here (`CHANGELOG.md`
   2026-08-18: *"z3660eth.o and `ld -r` exp byte-identical to the pre-change build"*).
3. **Code commits are proven gate-dominated by disassembly.** Every `f468` / `f448` / `4e71` /
   `4e7a` site in the cross-compiled object must be dominated by a `tstl` of the gate word and its
   `beq`. With `z3660eth_cache == 0` no cache opcode is reachable, so on the 030 and emulated-040
   lines the added cost is one `tstl`/`beq` per boundary and the behaviour is the old behaviour.

The emulated arms are additionally protected by construction — an interpreted CPU with no data
cache executes these words as no-ops even if the flag were wrongly poked — but that is a backstop,
not the argument. The argument is the gate.

---

## 7. F3-M3, ETHERNET ARM — THE PRE-REGISTERED METAL PROTOCOL

**Written before the session, per BLIZZARD §6.3. The bench structurally cannot run this**: Amiberry
models no 68040/060 copyback data cache, which is on `ACCEPTANCE.md`'s own list of things it is not
evidence for. Only the real 68060 can score it.

Same shape as round 1's §7 — same preconditions, same arm-A-first structure — with the payload
swapped for the RX/TX canary and the pass line taken from this driver's own measured bar.

### Preconditions (all four, or the run is void)

1. `z3660eth_direct_map == 1` and `z3660eth_ci_ok == 1` read back live from `/dev/kmem` — the free
   ride asserted, not assumed (F3-M0). `z3660eth_sptalloc_unsafe == 0`.
2. `z3660eth_dtt0`, `z3660eth_dtt1`, `z3660eth_cacr` captured into the evidence directory.
   `CACR.DPI` (bit 28) recorded; if set, the run is annotated, not silently accepted — a set DPI is
   the condition under which the *old* push-as-invalidate would have failed, so it is a finding.
3. The store buffer (`CACR` bit 29) is **OFF**. N5's hazard must not be armed during the run that
   scores the maintenance — one variable at a time.
4. Baseline thermal reading taken, and the F0 thermal policy in force (WARN 60 °C / +30 °C, ABORT
   70 °C / +35 °C), with the 15-minute real-CPU cap until one plateau is characterized.

### The A/B, and the prediction that differs from round 1

Same kernel, same driver object, both arms, arm A first:

- **Arm A — `z3660eth_cache = 0`.** The unmaintained driver on real caches.
- **Arm B — `z3660eth_cache = 60`.** The maintained driver.

> **PRE-REGISTERED PREDICTION: arm A PASSES.** This is the opposite of round 1's prediction and it
> follows directly from §1 — with `DTT0` broad, this driver's windows are cache-inhibited and it has
> no second alias anywhere, so there is nothing for the maintenance to fix. Writing this down is the
> point of pre-registration: **if arm A fails, the maintenance is NOT the automatic cure**, because
> by the same theory it should have been unnecessary. An arm-A failure means one of — the `DTT0`
> decode is wrong, the firmware/ARM store ordering is not what `ethernet.c` implies, or the window
> is not reached the way `z3660eth_map()` believes — and the next step is to read
> `z3660eth_ci_ok`/`dtt0`/`dtt1` and find out which, not to poke the flag and celebrate.
>
> **If arm A passes and arm B also passes**, the result is: the free ride is confirmed on real
> silicon, the maintenance is proven harmless and exercised (counters non-zero), and the driver is
> ready for the day `DTT0` narrows. That is a full pass, not a null result.

### The cycle (one cycle, repeated)

1. **RX canary — inbound bulk with whole-file verification.** FTP ≥ 3 files of mixed size
   (< 1 frame, ~64 KB, ≥ 3.4 MB) from the laptop to the box; checksum each on the box against the
   source. The ≥ 3.4 MB size is not arbitrary: it is the size the 2026-06-21 metal soak used.
2. **TX canary — outbound bulk with whole-file verification.** The same files back out, checksummed
   on the laptop. TX and RX have different choreographies (§5.1 vs §5.2) and a protocol that only
   exercises one of them scores half a driver.
3. **Concurrent load, running throughout:** a `find / -type f | xargs sum` sweep over the piscsi
   root. The concurrency is the point — it is what keeps the data cache dirty under the transfer,
   and it exercises both drivers at once, which is the shape F4 will actually run in.
4. **Flood ping**, 40 packets at 50 ms, laptop → box, per cycle.
5. **`netstat -in`** captured per cycle.
6. **Canary binaries**: checksum the same 8 binaries the cdfs case used — including
   `/usr/sbin/umount`, whose ELF magic is what caught the 2026-07-13 wild write — before and after.
   A driver cache bug that scribbles outside its window shows up here and nowhere else.
7. **Counters** read and recorded: `z3660eth_push_n`, `z3660eth_inv_n`, `z3660eth_push_bytes`,
   `z3660eth_inv_bytes`, `z3660eth_sync_n`, `z3660eth_range_ovf`, `z3660eth_ci_ok`,
   `z3660eth_sptalloc_unsafe`, plus the driver's own `rx_overrun` / `rx_bad_size` / `tx_errors`.

### Pass line, pre-registered

> **15/15 cycles clean on arm B**, where one clean cycle is all four of:
> - every transferred file byte-identical, both directions;
> - **40/40 flood ping, 0% loss** — the bar the 2026-06-21 metal soak was accepted at
>   (**measured**: *"laptop→box flood ping 40/40, 0% loss"*, `README.md` §"Status";
>   internal provenance `GRIMOIRE-HANDOFF.md:56`);
> - `netstat -in` showing **0 input errors, 0 output errors, 0 collisions** — same source;
> - **8/8 canary binaries byte-identical.**
>
> Plus, as gates on the counters rather than decoration:
> `z3660eth_range_ovf == 0`, `z3660eth_sptalloc_unsafe == 0`, `z3660eth_ci_ok == 1`, and
> **`z3660eth_push_n > 0` and `z3660eth_inv_n > 0`** — a green run with zero counters is a
> **failed** run, because it proves nothing was exercised.
>
> **Recorded, not gated:** sustained FTP throughput both arms. The 030 metal reference is
> **~185 KiB/s** (**measured**: *"sustained FTP throughput ~185 KiB/s (stable across runs)"*,
> `README.md` §"Status"; internal provenance `GRIMOIRE-HANDOFF.md:61`); the
> arm-A-vs-arm-B delta is the price of
> the belt and belongs in the record, but a throughput regression is not a correctness failure and
> must not be allowed to masquerade as one.

### Kill / fallback, pre-registered

- **Arm B fails with a named mechanism** ⇒ fix it and re-run; a bug report, not a re-scope.
- **Arm B fails three times with no named mechanism** ⇒ stop. Fall back to the documented, asserted
  free ride (F3-M0) with `z3660eth_cache = 0`, declare the maintenance arm parked, write the failure
  up. A documented dependence, never silence (BLIZZARD §6.3).
- **Arm A fails** ⇒ see the prediction above. Do **not** treat arm B passing as the explanation
  without first accounting for why §1's argument was wrong.
- **The assert fires** (`z3660eth_ci_ok == 2`) ⇒ the run is void for correctness purposes and the
  finding is the assert itself: the free ride is gone, and F4's driver story becomes the low-map
  classification question in `amix-040-060-port`, not a driver question.

---

## 8. WHAT THIS ROUND DELIBERATELY DOES NOT DO

- **The `sptalloc` arm is not made safe** — it is declared unsupported, detected and counted
  (§3.4). Making it safe needs two things this driver does not own: a `CM`-carrying device leaf in
  `hat040.s` (that repo's deferred TODO), and a `vtop` of each line's VA to feed the physical-line
  ops. Both are a later milestone.
- **The interrupt-driven RX path is not re-choreographed.** `z3660ethintr()` is not wired into
  `kernel.c` (**measured**, and the file says so at `:694-702`). It shares `z3660eth_drain()`, so it
  inherits the RX choreography for free; if it is ever wired, its own barrier/ordering story is
  owed then, not now.
- **`N5`'s barrier is not validated** — the instruction is placed and gated, and the claim that
  `nop` serializes pending writes on the 68060 is **assumed** until read out of the MC68060UM. It
  costs nothing today (the hazard is unarmed) and the manual check is owed before the store-buffer
  rung.
- **Nothing is proven about real caches.** Everything here is proven against the toolchain, the
  object and the `nm -u` gate. §7 is the only thing that can score correctness, and it needs the
  metal.
