# NOTES — amix-z3660net

## Probe failure leaked kernel VA (2026-09-27, AB-04)

**Rule: map, probe, free on failure.** Anything `z3660eth_map()` maps before
the probe is released on every failure path (`z3660eth_unmap()`, `sptfree(va, n, 0)`;
flag 0 because these are board pages, not `page[]` frames).

**The leak.** On the sptalloc arm (board at or above `VSECT1`, e.g. `autoconfig_rtg YES`
putting the combo window at 0x40000000) the map takes two windows: `ZZ_REGS_PAGES` (1)
and `ZZ_FRAME_PAGES` (32 at NBPP 4096; 64 on the 030 line). The failure returns after
them (sptalloc half-failure, the F3-M0 cache check, the MAC-sanity probe) only zeroed
`info->regs`; there was no `sptfree` in the file. So every failed open kept
**33 pages** (65 on the 030 line). `S99zen` opens `/dev/zen` **16 times** at boot.
On the AB-04 rig (amiberry, MAC registers read 0) that left **18 free sptmap pages**
(549 with S99zen disabled; the kvwalk shows 16 leaked 1+32 pairs), so raw I/O and
big kmem pools slept forever on `Km_BigWanted`: the DL-13 "stall". Evidence kept
locally, not published (amiberry AB-04 test runs: process, kvwalk and kstat captures).
Metal never failed the probe, so it never showed there; a metal firmware whose MAC
reads 0 would.

**Also changed.**
- A failure verdict is cached per board (`info->map_verdict`). Later opens return
  it without mapping or probing. `z3660eth_reprobe` (a `/dev/kmem` poke) forgets
  one verdict, for a firmware whose ethernet comes up late; there is no Zorro hot-plug.
- One console line per verdict, keeping the grep-able prefix:
  `zen: no Z3660 ethernet found (MAC reads 0x.. 0x..); opens fail errno 6`
  (or `not AGA`, `no board`, `cache-inhibit check refused`).
- `open` returns the real errno: `ENXIO` absent (cached), `ENOMEM` no kernel VA
  (not cached, printed once; the next open retries, which is now leak-free).
- Diagnostics: `z3660eth_map_fails`, `z3660eth_spt_freed`.
- `close()` never unmaps (a mapped board stays mapped; `map()` returns early on it),
  and the direct-map arm has nothing to free, so `z3660eth_unmap()` is the only teardown.

**Object proof** (040 driver line, gcc-cross-amix env.sh with the T-9 flags, the
`relink-040-z3660.sh` compile — that script lives on a private branch of the
040/060 port repo, not published; HEAD 47092ee source rebuilds to `b93ce42d`, as pinned):
`z3660eth_040.o` sha256 `a1f49c54d114cf76241abfa88bd60e46e500ada123a9c788d44cba5e153a2a4d`,
`sum -r` `20612 14`. `sptfree` is called twice in `z3660eth_unmap` (`pea 1`, `pea 0x20`,
flag 0), reached from the sptalloc-failure path in `z3660eth_map` and from
`z3660eth_absent` (the other three failures). G2: `z3660ethxmit` 3, `z3660ethopen` 3,
`z3660ethioctl` 2 `%sr` writes, unchanged. Board register accesses are unchanged,
all `movel`. The only new undefined symbol is `sptfree`, which the driver-line
kernel defines. `relink-040-z3660.sh` reports no unresolved symbols and 0 complaints.

## Interrupt masking (2026-09-27, review T-9)

**Premise corrected.** `spl6()`/`splx()` exist: they are `static inline` in the
stock `<sys/inline.h>` (`:112-260`). The old header no-opped them because a link
once showed them UNDEF; that came from not including `inline.h`. The cross build's
`-traditional` also erased the asm `volatile` until gcc-cross-amix `7a1d727`
(`-Dvolatile=__volatile__ -Dconst=__const__`), so masks written then would have
been deleted by the compiler anyway.

**Levels on this kernel.** `spl5`/`spl6`/`spl7`/`spltty`/`splhi`/`splstr` all
become `_spl4`, which writes `SR = 0x2400` (`inline.h:239-251`, `stream.h:461`).
Callouts dispatch at IPL 4, and only the level-2 CIA-A clock triggers them
(grimoire, "spl / callout-IPL contract"). So `spl6()` at base level blocks the RX
poll callout completely. `_spl4` *sets* the IPL rather than raising it. Every caller
here runs at IPL 0 (base) or IPL 4 (the callout), so the call only ever raises the
level or leaves it unchanged.

**Contexts.** There are two:
- **Base.** `open`, `close`, `wput`→`proto`→`xmit`, `wsrv`→`xmit`, `ioctl`.
- **The poll callout.** `z3660eth_poll`→`drain`→`toss_packet_up_stream`, and
  `tx_drain`→`xmit`.

The callout runs to completion before base resumes, and base never preempts it. So
a race needs a base-level sequence that the callout can land inside.

| Pair | Where | Race it closes |
|---|---|---|
| **M1** | `z3660ethxmit` claim: test `tx_busy`/`regs`, set `tx_busy` | The single `txwin` and the static `tmp[]` have exactly one owner. The test-and-set is one masked unit, so the claim does not depend on the run-to-completion argument. It stays correct if a level-2 ISR (`z3660ethintr` via `int2_tbl`) ever calls `xmit`. |
| **M2** | `z3660ethopen` slot publication: `EBUSY` test, field resets, `zp->q = q` stored last | The RX demux routes on `zp->q`, `flags` and `sap`. `close` clears only `zp->q`. Before this fix, `zp->q` was published *before* `flags`/`sap` were reset. A tick in between delivered frames up the new stream under the **previous occupant's** SAP or RAW flag. |
| **M3** | `Z3660ETH_GET_STATUS`: the `if_*` fills plus the 108-byte struct copy, with `allocb` kept outside | The callout bumps the counters (RX drain, deferred TX, `tx_nolink`, which is the only link-state indication). A tick mid-copy gave a torn snapshot, for example `packets_received` and `if_ipackets` taken from different ticks. |

**Removed.**
- The hydra-idiom mask around the whole of `wput`→`proto`. With real primitives it would hold IPL 4 across the ~1 ms synchronous TX trigger (ETHERNET-SCOPING Q2/Q7).
- The mask on the `tx_busy = 0` release. That is a single aligned store, and `tx_busy` is now `volatile`, so it stays after the TX-result load (object: `movel %a0@(400),%d0` then `clrl %a5@(24)`).

**Deliberately unmasked.** Each item below says why it is safe without a mask.
- **The RX ring.** This is the `ZZ_ETH_RX_ADDR` cursor, the `last_serial` head, the `ZZ_ETH_RX` strobe and the static `rxbuff`. Only the callout touches them, at IPL 4. A future level-2 ISR cannot interleave with the callout in either direction: the clock is level 2, and the callout masks level 2. The drain loop already runs at IPL 4 as a callout, so a mask would add nothing.
- **Hand-off to the stream.** `canput`/`allocb`/`putnext` come from the callout, and STREAMS protects its own queues with `splstr`.
- **`close`.** It stores `zp->q = 0` first as a single aligned store. The stop-poll sequence `poll_running = 0; untimeout(poll_id)` cannot be split in a harmful way: a tick between the two stores does not re-arm, and `untimeout` of an already-fired id is a no-op.
- **`DL_BIND`/`DL_UNBIND`.** The demux reads `sap` (a single store) and never reads `state`.
- **Known and accepted: reordering in `wsrv`.** `wsrv` can `getq` frame A, and a tick can then send the queue's frame B before `wsrv` claims `txwin`. Closing that would mean masking across the TX. DLPI connectionless service makes no ordering promise, and IP tolerates reordering.

**Object proof** (`z3660eth_040.o` sha256 `b93ce42d…`, `sum -r` `57252 12`). The per-function G2 gate checks spl call sites in the preprocessed body against `,%sr` writes:
- `z3660ethxmit` 3/3.
- `z3660ethopen` 3/3.
- `z3660ethioctl` 2/2.

The same source built without the new flags gives 0/0/0 (G1 and G2 both FAIL), so the gate is proven to catch the fault. The Z3660 register reads are 32-bit, for example `z3660ethintr` `movel %a0@(424),%d0` where the old flags gave `btst #0,%a0@(427)`.

**Still to verify on the bench.** Nothing in the three pairs is exercised by a boot alone. The DL cell has to run:
- zen0 bring-up;
- a sustained bidirectional flood, for example `ping -s 1400` both ways alongside an ftp transfer;
- repeated `slink` tear-down and re-open of zen0 while traffic arrives, which exercises M2;
- `zen` status polling in a loop under load, which exercises M3.

Pass criteria:
- no frames go to an unbound stream;
- `if_ipackets - packets_received` stays constant across snapshots within one open (the two are bumped back to back in the drain);
- no clock drift, which would indicate an over-long mask.
