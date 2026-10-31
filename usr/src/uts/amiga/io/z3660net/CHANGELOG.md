# Changelog

## 2026-10-13 — Publication scrub before the first push since August (fb2059e)

README licence section and a new NOTICE carry the Amiga Corporation trademark wording; NOTES marks the AB-04 evidence
as local-only and relink-040-z3660.sh as private port tooling; ZEN0-BRINGUP links amix-installng by its GitHub URL.

## 2026-09-27 — AB-04: free the mapping windows on a failed probe; cache the verdict (a3571ed, 586ba6f, 8449d53)

z3660eth_map() now sptfree()s both kernel-VA windows on every failure path; before, each failed probe on
the sptalloc arm leaked 33 pages (65 on the 030 line) and S99zen's 16 boot opens exhausted sptmap:
the rig's "DL-13 stall". A failed probe is cached per board (later opens return the errno without
mapping or probing again), one console line, real errno (ENXIO absent / ENOMEM no VA), z3660eth_reprobe
forgets one verdict. Object z3660eth_040.o a1f49c54 (sum -r 20612). Metal-proven inside KD060-Z (T-10).

## 2026-09-27 — Interrupt masking made real (4e75876, a33bb6c, 47092ee; review T-9)

The header no longer no-ops `spl6`/`splx` on the wrong "AMIX has none" premise: it includes the stock
`<sys/inline.h>` (spl5..7 map to `_spl4`, SR 0x2400). Three masked sections: M1 the `tx_busy` claim in
xmit; M2 open's slot publication (the RX demux could route frames up a new stream under the previous
occupant's sap/RAW flag); M3 the GET_STATUS snapshot. The hydra-idiom whole-wput mask and the mask on the
`tx_busy` release were deliberately not added. Design in NOTES.md "Interrupt masking". Rebuilt with
gcc-cross-amix 7a1d727: z3660eth_040.o b93ce42d… (sum -r 57252).

## 2026-09-09 — docs: the field-proven zen0 bring-up procedure; GRIMOIRE-HANDOFF cross-reference corrected (74d8564, ae808a7)

`docs/ZEN0-BRINGUP.md` records the bring-up as it was actually done on metal (major 51 verified on the
box 2026-09-09), so the next person does not rediscover it; the handoff's stale 48→51 renumber
cross-reference is corrected.

## 2026-09-01 — docs-currency sweep: README, BUILD, ETHERNET-SCOPING, BLIZZARD-F3

No code change; the public docs are brought level with HEAD.

- `BUILD.md` (`8a714a1`): §4 claimed the committed clean-gate script "predates" the `nm -u` bar
  and cited `GRIMOIRE-HANDOFF.md` §8 — contradicting the same file's own tail note, and citing a
  gitignored file no reader can open. `../amix-kerntools/tools/build-clean-net-kernel.sh` opens
  "THE BAR IS nm -u, NOT sum-recurrence", so the tail note was the true one. Both references to
  that script also spelled it as this repo's `tools/`, where it does not exist. "the locked
  two-tier routine in the project notes" now points at §7, which spells it out. The build box was
  described two ways (Amiberry + amix-golden.uae in §0, WinUAE + a config that resolves to
  nothing at the tail); the tail block is now marked the historical record. New §8 documents the
  cache gate, the kmem-readable verdict and counters, and why the direct-map arm is required.
- `README.md` (`9324b91`): cdevsw major **51** and the `/dev/zen0` (`c 51 0`) node — the two facts
  another driver author or packager needs first — appeared nowhere on the page; now in Scope.
  Layout gained `tools/`, `docs/BLIZZARD-F3.md` and `CHANGELOG.md`. "How it works" now names the
  mapping arm (direct dereference below VSECT1, `sptalloc` only above it), which the 040/060 DTT0
  free ride depends on.
- `docs/ETHERNET-SCOPING.md` (`9498305`): the banner still said the datapath was untested and
  named Phase 2/3 as NEXT — retitled to PROJECT STATUS — SHIPPING ON REAL HARDWARE with the metal
  result. §10.1's heading claimed the driver "does explicit line-granular 030 cache ops, always",
  which it has never done; heading and DECISION bullet now record that the prescribed 030 ops were
  designed and never shipped, and why. §5's Q-COHERENCY block and the §0 confidence table/verdict
  are annotated in place (the as-written review record is preserved). §8 gains a head note naming
  Q5 and Q6 as the only two items still open and a per-item marker on the nine that closed. §4.3
  said "seven" over a list of six and never acquired the DL_ERROR_ACK/DL_BADPRIM arm (`364333c`);
  both fixed. Four sites still carried 2026-07-27 for work that landed 2026-07-30 (`c949380`,
  `f12b2d5`) — `b9991e3` had fixed only driver.conf. The private `wip-ethernet` memory pointer is
  dropped and the `wip-ethernet`/`amix-base` branch plan marked superseded.
- `docs/{BLIZZARD-F3,ETHERNET-SCOPING-evidence}.md` (`98e269c`): `3b97dca` missed five
  `amix-z3660` → `amix-z3660scsi` sites in the evidence dossier and the SCOPING sources footer.
  BLIZZARD-F3's four **measured** citations — including both of the pass line's own anchors —
  pointed at the gitignored `GRIMOIRE-HANDOFF.md`; the values are inlined and re-cited to
  README §"Status" and `src/z3660eth.c:46-47`, with the handoff line numbers kept as internal
  provenance.

## 2026-08-24 — z3660eth: 040/060 cache maintenance behind a run-time gate (BLIZZARD F3 round 2)

The dead `Z3660ETH_CACHE_FLUSH` `#ifdef` is gone. `src/Makefile` never passed the -D, so the
block it hid had been compiled out of every kernel that ever booted and was four defects deep
when finally read. Maintenance is now always compiled and gated at run time by `z3660eth_cache`
(0 = off, the shipping default; 40 = 68040 DC, 60 = 68060 DC, poked through /dev/kmem), so the
same relocunix serves the 030 and the 040/060 lines and the code is reviewable rather than
invisible.

- **F3-M0 (`571f4d4`)** — attach-time assert that the free ride is real: `z3660eth_ci_check()`
  captures DTT0/DTT1/CACR and requires `DTT0 = 0x003fc060` to leave the whole low 1 GB, both
  mapped spans included, cache-inhibited. Verdict readable through /dev/kmem as
  `z3660eth_ci_ok` (0 unchecked / 1 verified / 2 FAILED) with `z3660eth_maint_on` and
  `z3660eth_sptalloc_unsafe`; `z3660eth_ci_enforce` decides whether a failure warns or refuses
  the attach with ENXIO. The `sptalloc` mapping arm is declared **unsupported with a live data
  cache** — its VA has no relation to the board's physical address, and CPUSHL/CINVL select a
  line physically, so maintenance disarms there rather than discarding some other subsystem's
  dirty line.
- **F3-M2 (`1bdaecb`)** — the previously identical PUSH/INVAL pair is split into real `cpushl`
  and `cinvl` raw `.word`s (f468 / f448, encodings measured both directions against as 2.8.1),
  because CACR.DPI gates CPUSHL's invalidate on the 68060 and push-as-invalidate silently stops
  invalidating there. Six line-op counters (`z3660eth_push_n`, `_inv_n`, `_push_bytes`,
  `_inv_bytes`, `_sync_n`, `_range_ovf`) are kmem-readable.
- **Docs (`4820333`, `084a945`, `df23adc`, `d71886c`, `b4371d4`)** — round 2 pre-registered
  before it was written (`docs/BLIZZARD-F3.md`); the "no PG_CI bit" claim corrected to an 030
  fact, not a universal one (the 040/060 kernel writes a two-bit leaf CM instead); the three
  ETHERNET-SCOPING §10.1 claims that do not carry to 040/060 marked; the result appended with
  nothing above it edited; README's cache paragraph rewritten.
- **Evidence (object-level, no metal):** `nm -u` byte-for-byte identical to the baseline; all
  six 040/060 instruction sites (`movec` ×3, `cpushl`+`cinvl`, `cinvl`, `nop`) proven dominated
  by a `tstl z3660eth_cache` / `beq` in the disassembly; the comment-only correction commit
  byte-identical at the object level (`z3660eth.o` and `ld -r exp`, sha256 `736afcc2…`);
  `-Wall` unchanged.
- **Nothing here is evidence about a real cache.** The §7 metal A/B soak is still owed, and a
  green run with zero counters would be a failed run.

## 2026-08-18 — z3660eth: page-size-agnostic window geometry (ded6ef4)

The frame-window math is now bytes-primary: ZZ_FRAME_BYTES (0x20000) and
ZZ_REGS_BYTES (0x1AC = register file 0x104..0x1A8 read as longs) are the
constants; ZZ_FRAME_SPAN/ZZ_WINDOW_TOP come straight from ZZ_FRAME_BYTES and
ZZ_FRAME_PAGES/ZZ_REGS_PAGES round UP from bytes over NBPP (sptalloc sizing on
the non-direct-map path only). ZZ_FRAME_SIZE stays 2048 — that is the firmware
per-slot PROTOCOL STRIDE, not a page size, and only coincidentally equals NBPP
on this kernel. The old page-count-first shape put ZZ_WINDOW_TOP — the b89c5d2
direct-map admission bound — one rework away from landing on exactly
ZZ_TXFRAME_OFF on a 4096-byte-page kernel, i.e. no longer covering the TX
staging frame. Five #if/#error assertions now fail the build if the shape is
reversed. Cross-toolchain evidence: 64/0x20000/0x07EF0000 at NBPP=2048 and
32/0x20000/0x07EF0000 at NBPP=4096; z3660eth.o and ld -r exp byte-identical
to the pre-change build (no behavioural change on the shipping kernel).

## 2026-08-13 — z3660eth: direct board mapping below VSECT1 (b89c5d2)

z3660eth_map() takes the physical base as the kernel VA when the whole span stays
under 0x40000000 (section-0 early-termination identity map — AMIX does NOT use the
68030 TT registers; ETHERNET-SCOPING §10.1); sptalloc kept only for a board at/above
VSECT1 (autoconfig_rtg). The metal 0x10000000 case now draws 0 sptmap pages (was 65).
Metal-proven: flew in the Route-A v3/v4 kernels at 88 MB with zen0 up.

## 2026-07-31 — on-box patch scripts made fail-closed (half-patch audit packet B)

- `tools/patch-kernel.sh`, `tools/patch-makefile.sh` (`603c719`): the name-wide
  `grep z3660eth` guard is replaced by ROW-SCOPED / SHAPE-SCOPED pre-flight, each edit
  is its own independently guarded ed invocation, and every arm is POST-VERIFIED in the
  file — ed's exit status is 0 through all three measured failure modes.
  Outcomes are now distinguishable: rc 0 `ALREADY PATCHED` / rc 0 `PATCHED:` /
  rc 4 `REFUSED (<verdict>)` with the file untouched / rc 5 `HALF-PATCHED` naming the
  failed arm and its `.bnkorig`.  Verdicts share kerntools `cdevsw-gate.sh`'s vocabulary
  (FREE, OURS, TAKEN, HALF, MISSING, MOVED, + AMBIGUOUS/NOANCHOR/NOKERNELC).
  Fixes audit F1 (a pre-48→51-renumber tree was reported ALREADY PATCHED, leaving slot 51
  unclaimed while renumbered `/dev/zen0` nodes opened 51 — silent no-ethernet) and both
  F5 half-patches (respelled `aeninfo` extern anchor; stripped `# kdb/exp` OBJ anchor
  leaving the rule-only state the guard then hid forever).  Trees already in either half
  state are now REPAIRED, not reported as done.  The replacement cdevsw row and its
  `/*51=z3660eth*/` tag are byte-unchanged (cross-repo drift-guard contract).
- `tools/patch-selftest.sh` (new, `603c719`): benchless host-side battery — 22 fixture
  scenarios per shell under `sh` and `dash`, bashism lint, an `ed` shim that drops an arm
  to exercise the post-verify, and the row-drift check kerntools' gate selftest depends on.
- `BUILD.md` (`d382108`): new §3b documents the outcome table, the verdicts, the optional
  path argument and the selftest; renumber date corrected 2026-07-27 → 2026-07-30.

## 2026-07-30 — cdevsw major renumbered 48 → 51; Q1 (AMIX level-6 servicing) resolved

Renumbered `z3660eth`'s char major **48 → 51** repo-wide (`c949380`, date note corrected
`b9991e3`). 48 was chosen in 2026-06 only because it was an empty `nostr` row on the
stock/a4091 kernel; the 2026-07 community sweep shows it is contested — `zz9000-amix`'s
`integration/kernel.c.txt` documents an unpublished dev tree using "47 for Hydra, 48 for
random, 49 for ZZ9000, 50 for sad", 49 (zz9000) and 68 (va2000) are published claims, and 47
is a live double claim by hydra and the stock 1992 `contrib/mx` driver. 51 is the lowest slot
of the ecosystem-verified-free set 51–67/69 (CDEVSIZE = 70; 7/38/39/44/45 are traps).
Convention and evidence: grimoire-amix's Major-Number Registry.
Sites: `driver.conf`, `tools/patch-kernel.sh`, `userland/{S99zen,bringup.sh,network-config.zen}`,
`BUILD.md` (carries the authoritative why-51 note), `docs/ETHERNET-SCOPING.md`,
`docs/ETHERNET-SCOPING-evidence.md` (supersession banner + prescriptive sites).
**Docs-and-config only — built kernels and shipped `/dev/zen0` nodes still carry 48 until the
next build (P5.4). A 51-built kernel with a stale `c 48 0` node opens an empty cdevsw row, and
`S99zen` only mknods when the node is absent — existing roots need the stale node removed or
re-mknod'd at P5.4.** (resolved: major 51 metal-verified 2026-09-09, see docs/ZEN0-BRINGUP.md)

Q1 RESOLVED (`f12b2d5`) — `docs/ETHERNET-SCOPING.md` §2.5 now reads resolved instead of OPEN.
AMIX does wire the level-6 autovector (`vec.s:36` → `p6int`), but `p6int` (`ttrap.s:118-134`) is
hardwired to CIA-B: it gates on Paula `INTREQR` bit 13, dismisses only Paula's latch, and calls
the single fixed handler `aciabintr`. There is no `int6_tbl` and no level-6 registration API.
So the Z3660's `/INT6` passes the gate but nothing ever acks the board, and because `/INT6`→EXTER
is level-sensitive it re-latches on every `rte` — the unbounded IPL6 storm behind the 2026-06-21
hard-lock. Confirms the shipped `ZZ_CONFIG_DISABLE` + bounded polled drain (`028c8d2`) as correct
architecture, not a workaround.

## 2026-07-26 — z3660eth defect packet (P0.3): three deferred STREAMS defects fixed

Found during the 2026-07-13 STREAMS "out of blocks" investigation and deliberately deferred then
to avoid perturbing a live RX-path hunt (that hunt closed: the wedge was the cdfs page-crossing
DMA wild write, since fixed). Host cross-build verified only — **bench/metal validation owed.** (still owed as of 2026-09-26; no run on record)

- **[HIGH] RX trailer-arm removed** (`54729bb`). `toss_packet_up_stream()` carried stock `aen.c`'s
  4.2BSD trailer decode verbatim. An ethertype in 0x1000–0x1010 made `length = (sap-0x1000)*512`
  reach 8192 and read the trailer-count word at offset up to 8208 of the 1518-byte `rxbuff`
  (~6.7 KB out-of-bounds read); the derived `trailn` could go negative, making
  `allocb(length+trailn)` smaller than the following `bcopy(..., length)` — an mblk heap overflow.
  Dead for legitimate traffic (SVR4 IP never negotiates trailers; `IFF_NOTRAILERS`;
  `ifconfig -trailers`) but reachable by any crafted frame off the wire. Deleted rather than
  repaired. **Normal RX is byte-for-byte unchanged** — proven by exhaustive case split on the
  ethertype guard and confirmed by cross-disassembly (the range test, the ×512 shift and the
  indexed OOB read are gone; the former else-arm is now unconditional; nothing else in the
  function changes). Frames in 0x1000–0x1010 now demux on that sap, bind to no stream, and count
  as `rx_no_sap` — correct handling for an encapsulation this stack never negotiated.
- **[MED] ioctl NULL-deref fixed** (`cd97e53`). `Z3660ETH_NUMBER_OF_BOARDS` and
  `Z3660ETH_GET_STATUS` dereferenced `mp->b_cont->b_rptr` with no NULL check (the same defect is
  still live in stock `aen.c`); an `I_STR` ioctl with no data panicked the kernel from any process
  able to open `/dev/zen0`. New `z3660eth_ioc_arg()` / `z3660eth_ioc_nak()` helpers add the NULL
  and short-message checks and give the driver one NAK path.
- **[LOW] three items** (`364333c`): `DL_ERROR_ACK`/`DL_BADPRIM` now emitted for unknown DLPI
  primitives (previously silently freed, hanging the DLS user); `z3660eth_tx_drain()`
  `return` → `break` (one blocked stream no longer starves the other four for a poll tick);
  `Z3660ETH_CLEAR_STATUS` / `Z3660ETH_GET_CONFIG` given explicit EINVAL-NAK case labels.

Build evidence: driver cross-compile diagnostics byte-identical to baseline at every step and
`nm -u exp` symbol set unchanged (27); full kernel cross-link green (`unix` 1575484 B, `nm -u`
resolved, `unix.brel` 29250 relocs, `relsim.py` **0 complaints**).

Owed at the next bench run: RX soak confirming normal IP/ARP unchanged + `rx_no_sap` ticking for a
crafted 0x1000-ethertype frame; the status tool's TRANSPARENT ioctl still correct plus a malformed
`I_STR` returning EINVAL instead of panicking (needs a small test program that does not exist yet);
a bogus DLPI primitive drawing `DL_ERROR_ACK`; multi-stream TX exercising the `break` path.
