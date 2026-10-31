# AMIX Native STREAMS/DLPI Ethernet Driver for the Z3660 — Design & Scoping Document (FINAL)

Target: **`z3660eth`** — a native AMIX (Commodore Amiga Unix SVR4.0, m68k 68030) STREAMS/DLPI Style-1 connectionless Ethernet provider for the Z3660 accelerator's onboard Zynq GEM, spoken via the existing Z3660 firmware mailbox (the network analogue of the `z3660.c` SCSI driver).

> This FINAL revision merges all valid corrections from four adversarial review lenses (protocol-correctness, coherency-mmu, dlpi-streams-completeness, build-integration). Corrections are tagged inline as **[FIX Bn/In/Mn — lens]**. Genuine reviewer disagreements/uncertainties are recorded as **OPEN QUESTIONS**, not silently resolved.

---

## ★ PROJECT STATUS — SHIPPING ON REAL HARDWARE (2026-06-21)

**z3660eth compiles clean, links into a clean kernel, BOOTS to `login:` (no panic), registers in its cdevsw slot, and `open(/dev/zen0)` returns `errno=6 ENXIO`** (the lazy-open probe correctly finds no GEM on WinUAE) — the box survives. That was the Phase-1 acceptance gate. Driver fixes committed in `amix-z3660net@9d12f3b`; corrected build tooling in amix-kerntools `tools/build-clean-net-kernel.sh` — the clean-gate bar became *`nm -h -u unix` empty, then `checkunix`*, replacing the `sum -r` recurrence wait (published in the public amix-kerntools history from its initial import).

**Phases 2–4 landed the same day on the real A4000+Z3660.** Full bidirectional TCP/IP over `zen0` @ `192.168.2.39`: 40/40 flood ping 0% loss, `telnet` and `ftp` over the interface, `netstat -in` showing 0 input errors / 0 output errors / 0 collisions, ~185 KiB/s sustained FTP. The only real-HW blocker was the firmware's INT6 storm — RX ships as a bounded polled drain instead (§10.2). **Everything below this banner is the as-written design record, kept for its reasoning and its review trail;** for what the driver does today read `README.md` "Status", and `CHANGELOG.md` for every change since.

Three things this scoping did NOT anticipate, learned during bring-up:

1. **The clean-kernel build bar is `nm -h -u unix` empty, NOT `checkunix`.** `relocunix` is literally `ln unix relocunix`, gated by the kernel Makefile's own `nm -h -u unix | egrep -v '(etext|edata|end)'` test. `checkunix` only validates symtab structure, so it passes a `unix` that still has undefined symbols (which the Makefile then rejects). The prior gate used the wrong bar and never produced a booting kernel. Fixed in `build-clean-net-kernel.sh`.
2. **AMIX has NO `spl6()`/`splx()` primitives** — the draft assumed BSD/hydra spl. Stock `aen` and `z3660.c` do ZERO interrupt masking; referencing `spl6`/`splx` left them UNDEF in the link. Fixed: no-op the spl macros in `z3660eth.h`. Proper RX/callout synchronization stays a Phase-3 concern (no spl primitive exists; revisit when RX leaves the polled `timeout()` callout). **→ CORRECTED 2026-09-27 (review T-9):** the premise was wrong — `spl6`/`splx` are `static inline` in stock `<sys/inline.h>`; the UNDEF came from not including it. Real masking is now in place; design and races in `NOTES.md` "Interrupt masking".
3. **A fresh `/usr/sys` rebuild faults at `$40000000` (z3mem) on WinUAE** unless the amix-base memory adaptation is in the build — independent of z3660eth (stock/golden kernels fault identically; only the installed amix-base `OLDunix` booted). The user resolved this 2026-06-21. The WinUAE config that boots is `AmigaUnix-mywork.uae` (xfer memory). **Always build/test on a COPY (`Amix-z3660-mywork.hdf`), never the original.**

---

## 0. CONFIDENCE & WHAT-REMAINS-UNVERIFIED

| Conclusion | Status | Basis |
|---|---|---|
| Register map / offsets / MAC packing / TX window / 4-byte slot header / INT bits | **SOURCE-CONFIRMED (HIGH)** | Cross-verified guest (`device.c`, `z3660_regs.h`) ↔ firmware (`ethernet.c`, `rtg.c`, `memorymap.h`). Every value agrees byte-for-byte. |
| Firmware asserts eth IRQ as **Amiga INT6** (FPGA_INT6), not configurable to INT2 in-firmware | **SOURCE-CONFIRMED (HIGH)** | `interrupt.c:29,50`; `interrupt.h:19`. The INT2 path is a guest-side board-mod, not a firmware option. |
| RX cursor advances **only** on `REG_ZZ_ETH_RX` strobe; `0x1A4` must be **re-read after every strobe** | **SOURCE-CONFIRMED (HIGH)** | `ethernet.c:596-597,604-639`; `rtg.c:1613`; `device.c:796-799`. |
| Empty-ring sentinel `serial==last_serial` works **because firmware stamps slot-0 serial = last-consumed on catch-up** | **SOURCE-CONFIRMED (HIGH)** | `ethernet.c:619-629`. |
| Firmware does **NOT** call `XEmacPs_SetOptions(FCS_STRIP)`; guest subtracts only the 14-byte header (no `-4`) | **SOURCE-CONFIRMED (MEDIUM→HIGH)** | `ethernet.c:540` raw `XEmacPs_BdGetLength`; `device.c:643` `datasize = sz - 14`, no CRC subtraction. **Exact FCS-presence still real-HW-confirmable** (see Q4). |
| TX rc==1 = `ethernet_task_state != ETH_TASK_READY` (no link) — normal until carrier | **SOURCE-CONFIRMED (HIGH)** | `ethernet.c:1031-1034`. |
| `REG_ZZ_CONFIG` enable (`1`) and ack (`8\|16`) are **mutually-exclusive** firmware branches (bit-3 gated) | **SOURCE-CONFIRMED (HIGH)** | `rtg.c:1150-1173`. |
| No firmware change required (interface is guest-agnostic) | **SOURCE-CONFIRMED (HIGH)** | `ethernet.c`/`rtg.c` eth paths: no OS branch, no `a3000_amix_mode`-equivalent. |
| **AMIX maps the Zorro III window CACHEABLE on the 030 (TT0/TT1 CI=0)** — the draft's "it's CI" premise was FALSE | **SOURCE-CONFIRMED (HIGH)** | `ttrap.s:327,329` literally comment `CI=0`; `z3660.c:139-142` `sptalloc(...,PG_V,...,0)` carries no visible CI flag. **→ see §10.1's 2026-08-13 mechanism correction: AMIX `pmove`s zero into `%tt0`/`%tt1`; the mechanism is the section-0 early-termination descriptor, not a TT range. The cacheable conclusion stands.** |
| `z3660.c` (SCSI) is **NOT** a transferable coherency precedent (it's synchronous + MMIO-bounced; eth RX is async) | **SOURCE-CONFIRMED (HIGH)** | `z3660.c:28-31,220-228` synchronous completion; `NOTES.md:48-50` SCSI coherency "🟡 unverified". |
| STREAMS/DLPI skeleton (open/close/wput/proto/`toss_packet_up_stream`/streamtab) reusable from `aen`/`hydra` | **SOURCE-CONFIRMED (HIGH)** | `aen.c`, `hydra.c` full read. |
| DLPI must answer **DL_ATTACH/DL_DETACH** too (hydra does; design's 5-primitive list was aen's) | **SOURCE-CONFIRMED (HIGH)** | `hydra.c:873-892,996-1015` vs `aen.c:789-926`. |
| Kernel wiring = 3 `kernel.c` edits + subdir Makefile (no fmodsw/unix.c) | **SOURCE-CONFIRMED (HIGH)** | `kernel.c`, `driver/Makefile`. |
| **The golden build-box `kernel.c` may NOT contain `hydra*` lines** (hydra is third-party; box derives from a4091 build box) | **UNVERIFIED — must pull live `kernel.c` first** | `amix-kerntools/README.md:21-22`. Anchor on `aen*` (stock), not `hydra*`. |
| Whether `sptalloc`'s 4th `flag` arg can request a cache-inhibited PTE | **UNVERIFIED — `immu.h`/`PG_*` are in the licensed sysroot, not readable here** | The actual 030-side coherency control point; Phase-0 deliverable. |
| **The entire TX/RX datapath, coherency, latency, and ARP/ping** | **REAL-HW-GATED (LOW until tested)** | WinUAE box emulates an a2065, NOT the GEM. It can only prove compile/link/boot/register/ENXIO. |

**One-line verdict:** FEASIBLE and overwhelmingly AMIX-side. The static protocol contract is solid; the RX *dynamics*, the *coherency mechanism*, and the *DLPI completeness* each had real bugs in the draft that this revision fixes. The single highest residual risk — **030-side cache coherency of the shared eth DDR window** — is **worse than the draft assumed** (the window is cacheable, not CI, and the cited SCSI precedent does not transfer), and is resolvable only by establishing `sptalloc` cache policy in Phase 0 + a real-HW soak.

**POST-BRING-UP STATUS (2026-08-24).** The table and the verdict above are kept as written — they are the review record — but the rows that were unsettled have since been answered:

- *"The entire TX/RX datapath, coherency, latency, and ARP/ping — REAL-HW-GATED (LOW until tested)"* → **tested; shipping.** Full bidirectional TCP/IP on the real A4000+Z3660 since 2026-06-21 (banner above, `README.md` "Status").
- *"Whether `sptalloc`'s 4th `flag` arg can request a cache-inhibited PTE"* → **it cannot: there is no `PG_CI` bit in this kernel at all** (§10.1). That is an 030 fact — the 040/060 kernel writes a two-bit leaf `CM` instead (§10.1's 2026-08-24 correction).
- *"The golden build-box `kernel.c` may NOT contain `hydra*` lines"* → **settled.** The edits anchor on `aen*` and ship as `tools/patch-kernel.sh`, which has run through several kernel builds; the anchor question is Q-BUILD-ANCHORS in §8.
- The verdict's *"resolvable only by establishing `sptalloc` cache policy in Phase 0 + a real-HW soak"* → **superseded.** Per-page CI is impossible, and the metal result showed the 030 needs no maintenance at all; the 040/060 line ops are compiled in but gated off by default (`docs/BLIZZARD-F3.md`).

---

## 1. Executive Summary + Feasibility Verdict

**Verdict: FEASIBLE, overwhelmingly AMIX-side — a port of the stock `aen`/`hydra` STREAMS/DLPI skeleton with the chip layer replaced by the Z3660 firmware mailbox.** The firmware already moves whole Ethernet frames to/from the Zynq GEM for AmigaOS `Z3660Net.device`; the contract is OS-agnostic (Survey B §5), so **no firmware change is required** (§3) — exactly the `z3660.c` SCSI story.

**Biggest risks, ranked (revised after review):**

1. **★ 030-side cache coherency of the shared eth DDR window — and the draft's two prescribed fixes were both unsound.** [FIX B1/B2 — coherency-mmu] The window is **cacheable** on the 030 (TT0 `CI=0`, `ttrap.s:327`), not CI as the draft claimed; and `z3660.c` provides *no* coherency precedent because SCSI is synchronous+MMIO-bounced while eth RX is asynchronous. The real precedent is the AmigaOS guest's commented-out `CacheClearE` at `device.c:817`. This is the most likely "works-then-corrupts" failure and is real-HW-only to clear. **Now treated as a hard Phase-0 deliverable** (establish `sptalloc` cache policy for the DDR frame pages) — see §5, §8 Q-COHERENCY.

2. **RX drain dynamics.** [FIX B1/B2 — protocol] The cursor advances *only* on the `0x194` strobe; the drain loop **must re-read `0x1A4` after every strobe** (the draft §4.5 pseudocode read it once and would under-drain/spin). The empty-ring sentinel rests on a firmware serial-stamp hack that must be documented. See §2.4, §4.5.

3. **RX length / FCS boundary — the documented ARP-killer, and it's source-answerable NOW.** [FIX I4/B2 — protocol/dlpi] Three sources use three conventions: `aen` subtracts `ETH_CRC_LEN`, the firmware delivers the raw `XEmacPs_BdGetLength` (no `SetOptions` FCS-strip), the guest subtracts only 14. **Do NOT inherit aen's `-4` blindly**; confirm with a one-frame length probe. See §2.4, §4.4, Q4.

4. **Synchronous/blocking TX with no TX-completion interrupt.** [FIX I5 — dlpi] The firmware busy-waits ≤~1 ms inside the `0x190` write, and there is **no TX-done IRQ**. The draft transplanted aen's defer-and-drain-from-TX-IRQ flow-control model onto hardware that lacks that IRQ — the deferred-TX path as written would stall. See §4.4, Q2.

5. **Build integration silently no-ops on the real box.** [FIX BLOCKER-1/BLOCKER-2 — build] The kerntools STREAMS edits anchor on `hydra*` lines that may not exist on the golden box, and the clean-gate's force-touch is `cd alien/`-hardcoded and cannot invalidate `driver/z3660eth/*.o` or `kernel.o`. Both fixable in Phase 0/1 but must precede any trusted gate run. See §6.

**Biggest correctness wins the draft already had (confirmed by review):** register map, MAC packing, TX window, slot header, INT6 identification, the `volatile`-everything rule, real-HW-only coherency verification, and the eager-drain mitigation for the 64-slot overrun.

---

## 2. The Z3660 ETH Interface AMIX Must Drive — Reconciled Protocol Contract

Surveys A (guest) and B (firmware) **agree on every value**. All offsets are relative to the Zorro III board base (`= cd->cd_BoardAddr` guest-side; the firmware's `RTG_BASE` mapping of that window). The driver gets `base` from Zorro autoconfig (mfg `0x144B`, prod `0x1` — the same board the SCSI/RTG side enumerates).

### 2.1 Register file (32-bit, big-endian, `volatile`, MMIO-emulated by firmware)

| Reg | Offset | Dir | Role |
|---|---|---|---|
| `REG_ZZ_CONFIG` | `0x104` | W | **enable = write exactly `1`; ack ETH = write exactly `24` (`8\|16`); disable = write `0`.** These are *distinct, mutually-exclusive* transactions. |
| `REG_ZZ_ETH_TX` | `0x190` | R/W | W byte-length = trigger TX; R = TX result (0=ok) |
| `REG_ZZ_ETH_RX` | `0x194` | W | strobe (value ignored) = advance/free current backlog slot |
| `REG_ZZ_ETH_MAC_HI` | `0x198` | R/W | MAC[0..1] = `(b0<<8)\|b1` |
| `REG_ZZ_ETH_MAC_LO` | `0x19C` | R/W | MAC[2..5]; write triggers GEM SetMacAddress |
| `REG_ZZ_ETH_RX_ADDRESS` | `0x1A4` | R | **board-relative byte offset** of current RX slot |
| `REG_ZZ_INT_STATUS` | `0x1A8` | R | pending bits: **ETH=1**, AUDIO=2, USB=4 |

> **[FIX I1 — protocol] `REG_ZZ_CONFIG` semantics, corrected.** The firmware branches on `zdata & 8` (`rtg.c:1150-1173`): if bit-3 is set it is an **ack/clear** path (`&16`→clear ETH) and the `&1` enable bit is **not examined**; otherwise `interrupt_enabled_ethernet = zdata & 1` (and writing `0` here *also* clears ETH). Therefore **you cannot enable and ack in one write** — a naïve `1|8|16` is taken as ack-only and enable is silently lost. Each CONFIG write also re-publishes the status word to the window (`rtg.c:1172`), a useful coherency property. Guest cross-check: `device.c:319` writes `1`, `device.c:85` writes `8|16`, `device.c:386` writes `0`.

### 2.2 Frame windows (real DDR, accessed directly through the Zorro III window)

- **TX staging:** `TX_FRAME_ADDRESS = base + 0x07EE0000`. Bare Ethernet frame at offset 0 (no length/header prefix; length goes via `REG_ZZ_ETH_TX`).
- **RX backlog ring:** base `RX_BACKLOG_ADDRESS = base + 0x07ED0000`; slot stride `FRAME_SIZE = 2048`; slot N at `0x07ED0000 + N*2048`. **The driver never computes slot N itself** — it reads `REG_ZZ_ETH_RX_ADDRESS` for the current slot's offset and follows it (the value is a *board-relative offset*; add `base`).
- **Per-slot 4-byte header (`RX_FRAME_PAD=4`), big-endian:** `[0..1]=length`, `[2..3]=serial` (16-bit monotonic, wraps), payload at `[4..]` (`dst[6] src[6] type[2] data…`). Ethertype for demux is at slot bytes `[16..17]`.
- **Publish order is the only RX barrier the 030 can lean on:** the firmware writes payload first, then the 4-byte header (serial last) (`ethernet.c:553-561`). On the 030 this ordering is meaningful **only if the 030 reads are coherent/ordered** — see §5.

> **[FIX I3 — coherency-mmu] The register page and the DDR frame pages are different beasts.** The `0x100`-page registers are **emulated MMIO** (the firmware traps the bus cycle — `main.c:644` "write to RTG registers is emulated"). The frame windows (`0x07ED0000…`) are **real DDR**. Whether each is CI on the 030 is a *separate* PTE question; do not treat "the board window" as one uniform region.

### 2.3 TX path

Build/copy the frame into `base+0x07EE0000` (cooked: synthesize 14-byte `dst|src|type` header from DL_UNITDATA dest + own MAC + bound SAP, then payload; raw: verbatim) → write byte-length to `base+0x190` → read `base+0x190`.

> **[FIX I2 — protocol] TX result codes, corrected.**
> - `0` = transmitted OK (firmware busy-waits ≤~1 ms for FramesTx, `ethernet.c:1080-1094`)
> - `1` = `ethernet_task_state != ETH_TASK_READY` — **GEM not ready / no link**, the *normal* steady state until carrier (`ethernet.c:1031-1034`). Map to "interface not ready/no carrier" → **queue/retry, do NOT count as `oerror`**.
> - `2` = BdRingAlloc fail (`ethernet.c:1053-1058`); `3` = BdRingToHw fail (`:1066-1070`); `4` = TX timeout (`:1085-1088`). These → `oerrors`++.

**Synchronous/blocking** (firmware busy-waits ≤~1 ms). One shared TX buffer — no TX pipelining.

### 2.4 RX path (corrected drain model)

On INT6: read `REG_ZZ_INT_STATUS`; if `& 1` (ETH), **ack** with `REG_ZZ_CONFIG = 24` (`8|16`), then **drain loop**:

```
loop (with loop_count cap, e.g. 256):
    off    = read REG_ZZ_ETH_RX_ADDRESS      // RE-READ every iteration — cursor moved on last strobe
    slot   = base + off
    serial = slot[2..3]
    if serial == last_serial: break          // ring drained (firmware stamped this sentinel — see below)
    len    = slot[0..1]
    <coherency: invalidate slot cache lines BEFORE this read if window is cacheable — §5>
    demux on ethertype slot[16..17]; copy `len` (minus FCS per Q4; cooked: also skip 14-byte hdr) into mblk; deliver
    last_serial = serial
    write REG_ZZ_ETH_RX = 1                   // advance/free this slot
```

> **[FIX B1 — protocol] The cursor advances ONLY on the `0x194` strobe; `0x1A4` MUST be re-read each iteration.** `ethernet_current_receive_ptr()` returns `RX_BACKLOG_ADDRESS + frames_received_from_backlog*FRAME_SIZE` (`ethernet.c:596-597`), and `frames_received_from_backlog` increments *only* inside `ethernet_receive_frame()` which fires *only* on the `REG_ZZ_ETH_RX` write (`rtg.c:1613`). The draft §4.5 pseudocode read `0x1A4` once then looped on serial → it would read the *same* slot forever (spin/under-drain). **Loop-drain is fine and preferable** to the reference's one-frame-per-IRQ model (the guest `frame_proc` `device.c:784-855` does one-per-IRQ and relies on the firmware re-nagging INT6 while backlog>0, `rtg.c:327-336`) — but only if `0x1A4` is re-read after every strobe.

> **[FIX B2 — protocol] The empty-ring sentinel depends on a firmware serial-stamp hack — document it.** When the consumer catches up, the firmware resets the ring AND stamps the now-current slot-0 with the just-consumed serial so the guest won't re-process it (`ethernet.c:619-629`). So `serial == last_serial` is the *intended* "ring empty" signal — it is **not** an inherently empty slot. Init `last_serial = 0` (safe: the firmware's first `frame_serial++` yields 1, and the guest also inits `old_serial=0`, `device.c:777`). The serial is 16-bit and **wraps**; keep the softc field `ushort`.

### 2.5 IRQ level — **Q1 RESOLVED (2026-07-30)**

> **[FIX M1 — protocol]** The firmware **unambiguously asserts Amiga INT6** (level-6 autovector): `amiga_interrupt_set(AMIGA_INTERRUPT_ETH)` → `DiscreteSet(REG0, FPGA_INT6)` on the 0→nonzero edge (`interrupt.c:29`); `AMIGA_INTERRUPT_ETH = 1` (`interrupt.h:19`). It is **not configurable to INT2 in this firmware** (the INT2 path is a guest-side `DEVF_INT2MODE` board-mod, `device.c:233-242`).

**The divergence:** the AMIX `aen`/`hydra` ISRs (and the Z3660 SCSI driver) are wired on **INT2** (`int2_tbl[]`); AMIX has no surveyed `int6_tbl[]`. **Q1 (was OPEN):** how does AMIX service a level-6 autovector, and does it have (or need) an INT6 ISR table? The level the firmware asserts (INT6) is fixed; the question was the *AMIX-side* hook. Answered against the AMIX trap vectors:

**Q1 RESOLVED (2026-07-30, from the relink kit's stock `amiga/ml` sources).** AMIX *does* service
the level-6 autovector: `amiga/ml/vec.s:36` wires vector offset `0x78` to `p6int` (levels 1–6 are
the only non-`nullvect` interrupt entries; level 7 is unhandled). But `p6int` (`ttrap.s:118-134`)
is **hardwired to CIA-B and nothing else**: it gates on Paula `INTREQR` bit 13 alone
(`and.w &0x2000` — EXTER; any other cause is treated as spurious), dismisses **only Paula's
request latch** (`mov.w %d0,0xDFF09C`), calls the single fixed handler `aciabintr`
(`driver/acia.c:42-58` — a CIA-B ICR read-clear plus floppy-index/CLOCK==2 branches; on the stock
CLOCK==1 kernel effectively a no-op), and falls into `intret`. **There is no `int6_tbl` and no
registration API for level 6** — the kernel's only ISR tables are the link-time-fixed `int2_tbl`
(`master.d/kernel.c:189-195`, walked by `p2int`) and the level-3 `vbinttab` (the only one with a
runtime API, `addvbint`/`remvbint`). Consequences: (1) the Z3660 FPGA's `/INT6` assertion *does*
reach this path (it sets EXTER and passes the gate — confirmed by the 2026-06-21 serial-PC
heartbeat showing the lock inside `p6int`/`aciabintr`/`intret`), but **nothing ever acks the
board**, and because `/INT6`→EXTER is level-sensitive the request re-latches on every `rte` → an
unbounded IPL6 storm that starves the level-2 CIA-A clock (all `timeout()` callouts, including
our own drain) and level-5 serial — the observed hard-lock. (2) Wiring an eth ISR at level 6
would require patching stock ml (`ttrap.s`) and relinking — kernel-source surgery outside the
driver-drop model. (3) A level-6 handler would run at IPL6, above the clock and serial — the
wrong place for per-frame RX work regardless. **Resolution: the shipped design is the correct
architecture, not a workaround** — `ZZ_CONFIG_DISABLE` so INT6 never fires, plus the bounded
polled drain (commit `028c8d2`). The §3 contingency stands as the only interrupt-driven path:
firmware routing eth notification to INT2 (PORTS, gate `0x0008`, `ttrap.s:33`) would be served by
the existing `p2int` walk via an `int2_tbl[]` entry — a one-line `master.d/kernel.c` edit at
kernel-build time, since the table is link-time-fixed; note every `int2_tbl` handler is called on
*every* INT2 with a fake-pcb-pointer argument and must identify and dismiss its own device.

### 2.6 MAC source

Read 6 bytes from `REG_ZZ_ETH_MAC_HI`/`_LO` (firmware-owned; default Commodore OUI `00:80:10:00:01:00`). Optionally write back to re-apply. Pack: `_HI=(m0<<8)|m1`, `_LO=(m2<<24)|(m3<<16)|(m4<<8)|m5`. This is the station address advertised in `DL_INFO_ACK`/`DL_BIND_ACK`.

### 2.7 Cache policy (firmware side) — and the corrected 030-side reality

Firmware maps the whole eth window (`0x07E00000–0x08000000`) **Normal Non-Cacheable** on the ARM side and does **asymmetric** maintenance: **TX flushes** (`Xil_L1/L2CacheFlushRange(TxFrame)`, `ethernet.c:1039-1040`) but **RX invalidates are commented out** (`ethernet.c:546-548`), relying on the non-cached ARM mapping.

> **[FIX B1 — coherency-mmu] The 030 side is CACHEABLE, not CI.** The draft asserted "the Zorro III window is CI, that's why SCSI works." **This is false per the actual TT registers:** `ttrap.s:327` `tt0_on = 0x003F0143 # …CI=0` and `:329` `tt1_on = 0x807F0143 # …CI=0`. The Z3660 combined window at `0x10000000` (NOTES.md; `z3660.c:59 Z3660_FIXED 0x10000000`) sits inside TT0's `0x00000000–0x3FFFFFFF` range → **cacheable on the 030**. SCSI works *over a cacheable mapping*, not a CI one. The CI-ness of the eth DDR pages must be **established, not assumed** (see §5, §8).

### 2.8 A-vs-B mismatches and latent hazards (explicit)

- **None on values.** Every register, offset, stride, header field, and IRQ semantic cross-checks between guest and firmware.
- **Latent firmware ring-overrun (not a driver bug to fix, but a driver mitigation):** `FRAME_MAX_BACKLOG=64` × 2048 = 128 KB, but the map comment says "32×2048" (64 KB) and `TX_FRAME_ADDRESS` starts exactly 64 KB above the ring base — a 64-deep undrained backlog overruns into the TX window. **Mitigation = drain RX eagerly** (the loop-drain model, §2.4). This *interacts* with the drain model: a one-per-IRQ ISR with RX bursts >32 frames between IRQs lets the firmware's producer overrun — a further argument for eager loop-drain. (Q6)
- **Multicast/promiscuous:** **no hardware filter register exists** (Survey A §5). See Q5.

---

## 3. Firmware-Change Decision

**No firmware change is required for the eth phase.** Every firmware path keyed on the eth registers (`rtg.c:803-809,1609-1646`; `ethernet.c` TX/RX) operates purely on register values + shared-buffer contents — **no branch on guest OS**, no `a3000_amix_mode`-style flag anywhere in the eth dispatch (Survey B §5). An AMIX driver reproducing the same register handshake + buffer/header layout is served identically — the SCSI story.

**Non-blocking caveats (driver-side design constraints, NOT firmware changes):**
- TX blocks the ARM ≤~1 ms inside the `0x190` store. Design AMIX TX to tolerate the stall (§4.4, Q2).
- **There is no TX-completion interrupt** — the firmware returns the result synchronously in the `0x190` read-back. This changes the flow-control model (§4.4) but requires no firmware change.

**Contingent change (only if Q1 resolves badly):** if AMIX cannot cleanly service the firmware's level-6 assertion, a firmware tweak to route eth notification to the SCSI mailbox's level would be a fallback — **speculative; investigate AMIX INT6 handling first.**

---

## 4. Driver Architecture — The Seam

**Principle:** keep the STREAMS/DLPI/`ifstats` machinery **byte-for-byte** from `hydra.c` (cleaner precedent, self-contained `init_tbl`); replace only the chip layer with the Z3660 mailbox datapath. Copy `hydra.c` → `z3660eth.c`, rename `hydra`→`z3660eth`. The seam is two flat buffers: TX `tmp[]` (just before the chip poke) and RX `rxbuff[]` (just before `toss_packet_up_stream`).

### 4.1 Data structures (reuse, rename)

- `module_info z3660eth_minfo` (tag e.g. `'ze'`, name `"z3660eth"`, MIN=60/MAX=1500, HIWAT=5120/LOWAT=2·HIWAT/3). **[FIX M8 — dlpi]** MIN/MAX are *packet bounds* (60/1500); HIWAT/LOWAT are the *separate water marks* — do not conflate.
- `qinit z3660ethrinit = {NULL,NULL, z3660ethopen, z3660ethclose, NULL, &minfo, NULL}` (read side: open/close; **no rput/rsrv** — RX is `putnext` from interrupt).
- `qinit z3660ethwinit = {z3660ethwput, z3660ethwsrv, NULL,NULL,NULL, &minfo, NULL}`.
- `struct streamtab z3660ethinfo = {&z3660ethrinit, &z3660ethwinit, NULL, NULL}` — **this symbol goes in `cdevsw[].d_str`**.
- Per-stream softc `z3660eth_t` (`q`, `state` DL_UNATTACHED/DL_UNBOUND/DL_IDLE, `board_index`, `flags` incl. RAW, `sap`). **[FIX I3 — dlpi]** keep `sap` as `u_short`; on BIND do the exact truncating assign `sap = (u_short)dl_sap` and the 2-byte no-swap copy onto the wire — see §4.4.
- Per-board softc — **rewrite the hardware half**: `volatile uchar *base`, derived register pointers (`base+0x104/0x190/0x194/0x198/0x19C/0x1A4/0x1A8`), `volatile uchar *txwin` (`base+0x07EE0000`), `volatile uchar *rxring` (`base+0x07ED0000`), `ushort last_serial`, `uchar paddress[6]`, board state.
- Aggregate `z3660eth_board[MAXBOARDS]` (MAXBOARDS=1 realistically) each containing the stream array, `aen_status`-style stats, hw softc, and `struct ifstats` (spliced into the global `ifstats` list on first open).

> **[FIX IMPORTANT-3 — build] The `ifstats` extern is a load-bearing type pun — copy it verbatim.** `kernel.c:185` defines the global as **`int ifstats;`** while `aen.c:43`/`hydra.c:36` declare `extern struct ifstats *ifstats;`. Same link symbol, conflicting C types; it links only because SVR4 `ld` doesn't type-check externs and `sizeof(int)==sizeof(ptr)` on m68k. **Copy `extern struct ifstats *ifstats;` verbatim; never redeclare** or the interface becomes invisible to `ifconfig`.

### 4.2 Entry points → kernel table

| Function | Role | Kernel table |
|---|---|---|
| `z3660ethopen/close` | minor→board/sap decode; lazy init; `ifstats` splice; stop GEM on last close | (read qinit → `cdevsw[].d_str`) |
| `z3660ethwput` | M_FLUSH / M_PROTO→proto / M_IOCTL→ioctl | (write qinit) |
| `z3660ethwsrv` | no-op stub (enables STREAMS back-pressure accounting) | (write qinit) |
| `z3660ethproto` | DLPI engine (§4.3) — reuse **hydra's 7-primitive** set | (called from wput) |
| `z3660ethioctl` | SIOCS/GIFFLAGS ACK, board-count/config/status | (called from wput) |
| `z3660ethxmit` | TX (§4.4) — rewrite hw half | (called from proto) |
| `z3660ethintr` | ISR (§4.5) — rewrite hw half | **see Q1** (`int2_tbl[]` vs INT6 hook) |
| `z3660ethinit` | boot probe → autoconfig; arm board state | **`init_tbl[]`** (mirror `hydrainit`) — **but see Q-INITMODEL** |
| `toss_packet_up_stream` | RX→DLPI: SAP demux + DL_UNITDATA_IND build + `putnext` — reuse almost verbatim | (called from ISR) |
| `transmit_interrupt` | TX-complete requeue — **see §4.4: Z3660 has no TX-done IRQ** | (called from ISR — but Z3660 has none) |

### 4.3 DLPI primitives — corrected against hydra: the **six** answered here, plus an explicit error arm

> **[FIX B1 — dlpi] Implement DL_ATTACH_REQ and DL_DETACH_REQ — the draft's 5-primitive list was aen's, not hydra's.** The draft said "copy hydra byte-for-byte" but then listed only the five primitives `aen` implements. **`hydra.c` answers seven**, including `DL_ATTACH_REQ → DL_OK_ACK(state=DL_UNBOUND)` (`hydra.c:873-892`) and `DL_DETACH_REQ → DL_OK_ACK(state=DL_UNATTACHED)` (`hydra.c:996-1015`). Even under STYLE1 (where a strict provider binds the PPA at open), the safe course is hydra's: answer ATTACH/DETACH with DL_OK_ACK. Hydra found it necessary to answer them — that is evidence the AMIX stack can send them; an unanswered/NAK'd ATTACH fails attach silently.

Full set (irreducible for ARP+ping, plus the two recovered above):

- **DL_INFO_REQ → DL_INFO_ACK:** `dl_mac_type=DL_ETHER`, `dl_service_mode=DL_CLDLS`, `dl_provider_style=DL_STYLE1`, `dl_max_sdu=1500`, `dl_min_sdu=1`, `dl_addr_length=6`, MAC at `dl_addr_offset` (only when bound).
- **DL_ATTACH_REQ → DL_OK_ACK** (state→DL_UNBOUND). **DL_DETACH_REQ → DL_OK_ACK** (state→DL_UNATTACHED).
- **DL_BIND_REQ → DL_BIND_ACK:** `sap=(u_short)dl_sap` (ethertype; ARP 0x0806, IP 0x0800), `state=DL_IDLE`, echo SAP+MAC.
- **DL_UNITDATA_REQ → `z3660ethxmit`** (TX).
- **DL_UNBIND_REQ → DL_OK_ACK.**
- **Consider** DL_PHYS_ADDR_REQ. Multicast/promisc: see Q5 (no hardware filter exists).
- **Anything else → `DL_ERROR_ACK` with `dl_errno = DL_BADPRIM`**, naming the offending primitive in `dl_error_primitive` (shipped 2026-07-26, `364333c`; the inherited `aen`/`hydra` behaviour silently `freemsg`'d the message and left the DLS user waiting for an ack that never arrived). The six `case DL_*` arms above plus this default are exactly what `src/z3660eth.c` implements.

### 4.4 TX flow (DL_UNITDATA_REQ → wire) — corrected gate + flow-control

1. **Gate (preserve ordering):** **[FIX I6 — dlpi]** use the exact `q->q_first || !z3660ethxmit(q,mp)` test (`aen.c:792`, `hydra.c:869`). The `q->q_first` half is **ordering preservation** — if anything is already queued, this frame queues behind it; the draft's "is the TX window free?" dropped it and would let frames jump the queue. On busy → `putq` (deferred).
2. **Assemble flat frame** into `tmp[]` (then `bcopy` to `txwin`): dest MAC from `mp->b_rptr + dl_dest_addr_offset`; src MAC = `paddress`; **ethertype = the bound `u_short sap`, copied as 2 bytes with NO byte-swap** ([FIX I3 — dlpi]: the m68k is big-endian; the low 16 bits land correctly — do not add `htons`). Walk `mp->b_cont`, cap `ETH_MAXDATA=1500`. `freemsg`. **Pad short frames** to the min — TX pad target is `ETH_MINDATA` (50 payload → 64-byte frame pre-CRC); **[FIX M10 — dlpi]** the GEM may auto-pad TX itself, so confirm and avoid double-pad (Q4-adjacent).
3. **Trigger:** write byte-length to `0x190`; **read it back** for rc; map per §2.3 (rc==1 = no-carrier → queue/retry; rc 2/3/4 → `oerrors`).
4. **★ Flow-control resume — there is NO TX-done IRQ.** **[FIX I5 — dlpi]** In aen/hydra, `wsrv` is a no-op and the deferred-TX backlog is drained by `transmit_interrupt` on the **TX-completion IRQ** (`aen.c:1357-1380`). **The Z3660 has no TX-completion interrupt** (TX is synchronous; the busy-wait returns the result in-register, `ethernet.c:1080-1090`). So aen's "defer, drain from TX-IRQ" idiom has **no event to hang the drain on**.

> **OPEN QUESTION Q2 (TX flow-control model) — two viable answers, pick during grilling/Phase-2:**
> - **(a) Never defer for "ring full":** since TX is synchronous, a frame either goes or returns rc; the only "busy" is *another thread mid-TX*, resolved by a **TX-serialization lock** (the single `txwin` must be serialized regardless — Q7). No `putq` backlog at all → simplest, matches the synchronous hardware. The ~1 ms stall is held under the lock, **not** at raised spl.
> - **(b) Keep `putq` deferral but drain via explicit `qenable(WR(q))`** immediately after each blocking TX returns (and/or from the RX ISR), since no TX-IRQ will do it. More faithful to the STREAMS idiom but adds a stall-management path.
> The draft implicitly assumed aen's IRQ-driven drain, which does not exist here. **Resolve before Phase 3.** Either way: **do not hold raised spl across the ≤1 ms `0x190` store.**

### 4.5 RX flow (INT6 → up the stream) — corrected drain + coherency hook

ISR (`z3660ethintr`): `if (panicstr) return`; per-board RUNNING gate; read `REG_ZZ_INT_STATUS`; if `& 1` → ack `REG_ZZ_CONFIG = 24` → `receive_interrupt(board)`.

`receive_interrupt` rewrite — the corrected drain loop of §2.4 (re-read `0x1A4` each iteration; serial-sentinel break; `loop_count` cap à la hydra's 256). **[FIX I4/I5 — coherency]** before reading the 4-byte header each iteration, **invalidate the slot's cache lines if the window is cacheable** (§5); the payload sits in the same/adjacent lines so it is then coherent. Copy `len` bytes (FCS handling per Q4) from `slot+4` into the static `rxbuff[]`; `last_serial=serial`; strobe `0x194`; fall through to `toss_packet_up_stream(rxbuff, board, len)`.

`toss_packet_up_stream` reuse almost verbatim: SAP = ethertype `[16..17]`; trailer branch present but dead under `-trailers` ([FIX M9 — dlpi]: it still *executes* the test, harmless); iterate streams; RAW streams get whole frame; else `sap`-matched streams get a `DL_UNITDATA_IND` (allocb header+data, fill dest/src MAC + payload, `canput(q->q_next)` check, `putnext`).

> **[FIX I4 — dlpi] "Broadcast flows naturally" is true ONLY because the non-promiscuous path does NO dest-MAC filtering** (`aen.c:1270` is a pure `sap==sap` match; dest filtering lives only in the promiscuous branch). The driver therefore **trusts the firmware GEM to have filtered to own-MAC+broadcast.** **OPEN QUESTION (folds into Q5):** confirm the Z3660 GEM RX filter mode — if it passes *all* traffic for the bound SAP, AMIX IP receives other hosts' unicasts and must drop them (wasted allocb+putnext per frame). Option: add an explicit dest-MAC accept-filter (own || broadcast) in the Z3660 `toss_packet_up_stream`.

### 4.6 Init / probe (and the WinUAE ENXIO)

- `z3660ethinit` → autoconfig: find Zorro board mfg `0x144B`/prod `0x1` (reuse the **proven Z3660 SCSI discovery**, NOT raw `autocon()` — hydra warns `autocon` corrupts; same board SCSI already enumerates). Read MAC from `0x198/0x19C`. **[FIX IMPORTANT-4 — build] The eth offsets are NOT mapped by the SCSI driver** — `z3660.c` only `sptalloc`s the register page (`+0x2000`) and a 64 KB bounce (`+0x80000`); the eth windows at `+0x07ED0000`/`+0x07EE0000` need **their own `sptalloc`** (RX ring ≥64 pages = 128 KB, TX window, non-contiguous → separate calls) **if** the board lands in the TT gap (`0x40000000`). If it lands at the fixed `0x10000000` (the normal config), the offsets are inside TT0 and reachable by direct deref like aen/a2091 — but **cacheable** (§5). **The cache flag passed to those `sptalloc` calls is the actual 030 coherency control point** (§5, Q-COHERENCY).
- **OPEN QUESTION Q-INITMODEL (build):** **[FIX IMPORTANT-2 — build]** `aen` has **no `init_tbl[]` entry** (lazy autoconfig on first `open`); only `hydra` boot-probes via `init_tbl`. The draft picked the hydra model, which means **`z3660ethinit` runs at boot on the WinUAE gate box** (which has no GEM) — the probe must be bulletproof against "board absent" or it risks the gate box's boot. Safer for Phase 1: copy **aen's lazy-open** model (no `init_tbl` entry), making "boots without panic" trivially true regardless of probe correctness; add the boot probe later if wanted. **Decide in Phase 0/1.**
- **On WinUAE the Z3660 board is absent** (it emulates an a2065), so autoconfig finds nothing → board count 0 → `open` returns **ENXIO** (exactly like `z3660.c`). Compile/link/boot/register succeed; the datapath has no board to drive — the safe-to-test property.

---

## 5. Cache-Coherency / MMU Handling (first-class concern — substantially revised)

**The core problem (corrected):** the firmware maps the eth window non-cacheable on the **ARM** side and does zero **030** maintenance. The 030 reaches these buffers through the Zorro III window, and — contrary to the draft — **that window is cacheable on the 030** (TT0 `CI=0`, `ttrap.s:327`). The eth RX path is **asynchronous** (firmware writes a slot at GEM-RX time; the 030 reads it later from the ISR), with **no synchronous completion handshake**, and freshness is a **serial word the firmware publishes last**. On a cacheable mapping the 030 can (a) read a **stale cached** copy of the slot/serial (missed or torn frame), and (b) **withhold/reorder** the TX frame in copyback cache so the firmware reads stale DDR. This is precisely the lpsched-/SCSI-bounce-class race.

> **[FIX B1/B2 — coherency-mmu] Both of the draft's prescribed fixes were unsound:**
> 1. **"The window is CI, so accesses are serialized/ordered"** — FALSE. `ttrap.s:327,329` show `CI=0`; `z3660.c:139-142` `sptalloc(...,PG_V,...,0)` carries no visible CI flag. **CI must be established, not assumed.**
> 2. **"Mirror `z3660.c` exactly"** — does NOT transfer. `z3660.c` has *zero* cache maintenance (no `cpush`/`cinv`/barrier; `z3660intr()` empty) and is coherent only because SCSI is **synchronous + MMIO-bounced** (`z3660.c:28-31,220-228`; the command write completes the whole transfer before returning; NOTES.md:48-50 calls even *that* "🟡 unverified"). Mirroring it gives the eth RX path **no defense at all** against the async race.

**Required handling (corrected, specific):**

1. **Establish the 030 cache policy of the eth DDR frame pages — Phase-0 hard deliverable.** Determine whether `sptalloc`'s 4th `flag` argument can request a **cache-inhibited PTE** (it is literally named `flag`; `immu.h`/`PG_*` are in the licensed sysroot, unreadable here — read it on-box). **If it can, USE it** for `0x07ED0000`/`0x07EE0000` (and the register page) — this is the clean fix and makes ordering implicit. The register page is emulated MMIO and may already need CI; the **DDR frame pages are the separate, load-bearing question.**
2. **If the DDR pages cannot be made CI**, issue explicit **68030 cache control** (NOT a barrier — the 030 has no `dsb`; **[FIX I3 — coherency]** the MEMORY "dsb was a Heisenbug" note is about the *ARM/firmware* side and does not license dropping 030 maintenance):
   - **RX:** `cinvl`/`cpushl` the RX slot's cache lines **before** reading the 4-byte header each drain iteration (then payload in adjacent lines is coherent), then strobe `0x194`.
   - **TX:** `cpushl` the TX frame **after** filling `txwin` and **before** writing the length to `0x190`.
   - This re-enables, in driver form, the very `CacheClearE(frm, …, CACRF_ClearD)` the AmigaOS guest left commented at `device.c:817` — the **real RX-coherency precedent** (the guest could leave it commented only because *its* Zorro III map is CI; AMIX's TT0 is not).
3. **Declare every shared access `volatile`** (registers and slot fields) — load-bearing, matches `aen.h:84-93`, `a2091.c`, `z3660.c:101-102`.
4. **RX staging decision is moot until cache policy is pinned** ([FIX I5 — coherency]): neither "read slot directly into mblk" nor "stage through `rxbuff[]`" is safe on a cacheable map without the invalidate in (2). The mblk destination is fine (hardware never touches it); the **frontier is the `bcopy` source (the slot)**, which may be read from stale 030 cache.
5. **Verification is real-HW-only** — the WinUAE box has no GEM. Plan an explicit **coherency soak** (sustained TX+RX under load, byte-verify) — the eth analogue of the SCSI `[WVERIFY]` campaign — before declaring the datapath sound.

> **★ OPEN QUESTION Q-COHERENCY (highest-ranked risk):** Can `sptalloc` produce a CI PTE for the eth DDR pages (clean fix), or must the driver do explicit `cinvl`/`cpushl` per handshake? Resolve the `sptalloc`/`PG_*` capability in Phase 0; **prove** the chosen mechanism on the real-HW soak. Until proven, the datapath is undefended against the design's #1 risk.
>
> **→ RESOLVED — §10.1, and not the way this section expected.** `sptalloc` cannot produce a CI PTE (no `PG_CI` bit on the 030), and the explicit `cinvl`/`cpushl` prescribed as the fallback **were never shipped**: the 2026-06-21 metal bring-up ran TX and RX byte-correct with no maintenance at all, and the shipped 030 driver issues none. The 040/060 line ops added 2026-08-24 are compiled in always and gated off by `z3660eth_cache = 0`. Authority: `docs/BLIZZARD-F3.md`; history: CHANGELOG 2026-08-24.

---

## 6. Kernel Integration + Kerntools (build-kernel.sh STREAMS path) — corrected anchors & gate

### 6.1 `master.d/kernel.c` edits (three insertions + externs)

Three insertions, **anchored on `aen*` (stock), NOT `hydra*`** — see BLOCKER-1 below:
1. **cdevsw row** — the driver's major is **51** (do **NOT** use 7 — [FIX M2 — build] slot 7 is a reserved `/*7=win?*/` row). The 13-field row:
   `ND,ND,ND,ND,ND,ND,ND,ND,ND,ND,notty,&z3660ethinfo,nullflag, /*51=z3660eth*/`
   and add `extern struct streamtab z3660ethinfo;` next to `extern struct streamtab aeninfo;` (`kernel.c:265`).

   > **Why 51 — renumbered from 48 on 2026-07-30.** 51 is the lowest slot of the ecosystem-verified-free set **51–67 and 69**. The stock table leaves 48, 49 and 51–69 empty, which is why 48 was picked in 2026-06 — but 48 is **contested** by an unpublished community development tree (`zz9000-amix`'s `integration/kernel.c.txt`: *"47 for Hydra, 48 for random, 49 for ZZ9000, 50 for sad"*), 49 is published `zz9000`, 68 is `va2000`, and 47 is claimed by hydra *and* the stock 1992 `contrib/mx` driver. `CDEVSIZE` = 70, so ≥ 70 does not exist without extending the array; 7/38/39/44/45 are traps. Full evidence and the cross-tree claim table: **`../grimoire-amix/docs/reference/major-number-registry.md`** (Major-Number Registry) — whose convention is "take 51–67 or 69, and respect an existing community claim even a weak one". The narrative note lives in `BUILD.md` §2(b). **48 is vacated — do not reuse it.**
2. **int2_tbl entry** (pending Q1) — add `z3660ethintr,` before the terminating `0};`, anchored after `aenintr,` (`kernel.c:193`); append `z3660ethintr` to the extern (`kernel.c:187`).
3. **init_tbl entry** — add `z3660ethinit,` before `0};` (only if using the hydra boot-probe model — see Q-INITMODEL); add to the extern (`kernel.c:38`).

No `fmodsw[]`, no `unix.c`, no second registry — the major↔streamtab binding is purely the cdevsw slot position.

> **[FIX BLOCKER-1 — build] Anchor on `aen*`, and PULL THE LIVE `kernel.c` FIRST.** Every line number/anchor in the draft came from the **`hydra-amix` reference clone**, but the gate box builds the **golden image's `/usr/sys`** (provenance: "a copy of the proven `Amix-dbg.hdf` build box from the amix-a4091 project", `amix-kerntools/README.md:21-22`). `hydra` is a third-party out-of-tree driver — **the golden `kernel.c` may have NO `hydrainit`/`hydraintr`/`hydrainfo` lines**, in which case every hydra-anchored sed silently inserts nothing and the driver links clean but is **never registered** (empty cdevsw slot → ENXIO that looks like a driver bug). **Phase-0 hard gate:** pull the live on-box `master.d/kernel.c` and `amiga/driver/Makefile`, verify which net drivers are present, and anchor on `aen*` (stock Commodore, far more likely present; `aeninfo` `kernel.c:265`, `aenintr` `kernel.c:193`). Note `aen` has **no `init_tbl` entry**, so if anchoring init on `aen` you must anchor on a different stable line (e.g. `sadinit,`) or adopt the lazy-open model (Q-INITMODEL). **No pristine `kernel.c` exists in kerntools today** — the "or keep known-good copies" hedge is unbacked.

### 6.2 Driver subdir + Makefile

New `amiga/driver/z3660eth/Makefile` (mirror `aen/Makefile`; note the deeper include path):
```
CFLAGS = -O -D_KERNEL -DSVR40 -DSVR4 -I../../.. -I../../inc
OBJ    = z3660eth.o
exp:   $(OBJ)
	ld -r -o exp $(OBJ)
clean:
	-rm -f $(OBJ) exp
z3660eth.o : z3660eth.h
```
Plus two insertions in `amiga/driver/Makefile`, **anchored on `aen/exp`** (not `hydra/exp` — [FIX IMPORTANT-1 — build] same provenance risk): add `  z3660eth/exp \` to `OBJ`, and a `z3660eth/exp :\n\tcd z3660eth; $(MAKE)` rule. The subtree is reachable (top `relocunix` → `cd amiga; make` → `driver/Makefile` recurses), **but only an explicit `rm` of the stale `exp`/`.o` guarantees the gate measures a kernel containing the edits** (§6.3).

### 6.3 build-kernel.sh STREAMS-path extension (corrected)

The SCSI path generates `scsicard[]` rows into `alien/sd.c` and never touches `kernel.c`; STREAMS cannot reuse it.

> **[FIX IMPORTANT-4 — build] The `driver.conf` parser needs a real type-dispatch refactor, not just "parse net stanzas."** Today's loop reads **positionally** (`while read -r pn fn rest; base="${fn%queue}"`, `build-kernel.sh:69,84`) and has **no type dispatch** — a `net …` line would be mis-parsed (e.g. `base=z3660eth.c`, then a bogus `scsicard[]` row with `&z3660eth.c` as a function pointer, or a `missing src` error). Adding `net` requires a `case "$pn" in net) … ;; *) <existing scsi> ;; esac` rewrite **with back-compat care so the proven A4091/Z3660 SCSI path does not regress.** The z3660 repo's `driver.conf` currently has only the SCSI line — the eth stanza lives in a new conf.

Proposed typed stanza:
```
net  z3660eth.c  major=51  streamtab=z3660ethinfo  init=z3660ethinit  intr=z3660ethintr  "Z3660 Ethernet"
```

New steps (delta):
1. Type-dispatch the read loop; keep `scsi` stanzas on the existing path unchanged.
2. Render `templates/net-driver-Makefile.in` → `work/z3660eth/Makefile`.
3. **Pull pristine on-box `kernel.c` + `driver/Makefile`** (BLOCKER-1); apply **three anchored, idempotent** `kernel.c` insertions (anchor on `aen*`; guard with a `/* z3660eth */` marker against double-insert) + the two `driver/Makefile` insertions (anchor on `aen/exp`).
4. Push: source(+headers) → `amiga/driver/z3660eth/`; subdir Makefile → same; patched `kernel.c` → `master.d/`; patched `driver/Makefile` → `amiga/driver/` (`amixsync.py push`, new remote paths).
5. **Run the clean-gate — but it needs a REAL code change, not "reuse unchanged."**

> **[FIX BLOCKER-2 — build] The clean-gate's force-touch is hardwired to `alien/` and cannot rebuild a `driver/` subdir or `kernel.c`.** `tools/build-clean-kernel.sh:27` does `cd /usr/sys/amiga/alien; rm -f $OBJS exp; touch $SRCS` — passing `z3660eth.c` makes it `touch` a file that doesn't exist in `alien/` (no-op), so `driver/z3660eth/z3660eth.o` is never invalidated. It removes `amiga/exp` and `master.d/exp` but **not** `master.d/kernel.o`, `amiga/driver/exp`, or `amiga/driver/z3660eth/z3660eth.o` — and the Makefiles have **no header-dep tracking** (MEMORY: wip-emulated-ram), so removing `master.d/exp` alone does NOT force `kernel.c → kernel.o` recompilation. **The gate can certify-clean a kernel that silently lacks the driver or its registration.** **Required fix:** for net drivers, `rm -f` the full on-box paths `amiga/driver/z3660eth/{z3660eth.o,exp}`, `amiga/driver/exp`, `master.d/kernel.o`, `master.d/exp`, and `touch` the correct subdir source each round — generalize the touch/rm to take **full on-box paths** rather than basenames `cd`'d into `alien/`. The gate's stabilization / `sum -r` recurrence / `checkunix` logic is reused verbatim; only its path handling changes.

### 6.4 Deploy / boot path (confirmed)

`make install` (`cd /usr/sys`) + `/stand make` is the proven SCSI flow (`build-kernel.sh:160`). **[FIX MINOR-3 — build]** Be explicit about which two-tier variant: the plan shuttles the **built `relocunix`** to the real box, but `/stand make` rebuilds the boot partition from the **staged** kernel — so the real box's `/usr/sys` source (`kernel.c` + `driver/z3660eth/` + patched `driver/Makefile`) **must match what was gated on WinUAE**, or the real box relinks a *different* kernel. Either shuttle the built `relocunix` and skip the real-box relink, or shuttle the full source set and relink on the real box — **consistently, not mixed.**

---

## 7. Userland Bring-up (real HW @ 192.168.2.39)

Major **51** (§6.1), interface **`zen0`**. `slink addaen` is **generic** ("link an Ethernet DLPI driver under IP+ARP", reused verbatim by hydra; `strcf:87-94` links the driver under **both** `/dev/ip` (with `app` pushed) and `/dev/arp`). SVR4.0 has **no `plumb`** and **literal IP only** (never `` `uname -n` `` — the ~90 s DNS-stall trap).

**One-shot (single-user/testing):**
```sh
mknod /dev/zen0 c 51 0
/usr/sbin/slink                                  # if boot didn't: strcf boot{} (tcp/udp/icmp/rawip + loopback)
/usr/sbin/slink addaen /dev/zen0 zen0            # links under /dev/ip AND /dev/arp
/usr/sbin/ifconfig zen0 192.168.2.39 netmask 255.255.255.0 up -trailers
/usr/sbin/route add default 192.168.2.1 1        # metric 1 MANDATORY (not a flag)
ping 192.168.2.1
```

> **[FIX I7 — dlpi] open()/close() must replicate aen's clone/dev_t machinery — the `slink addaen` double-open under IP+ARP depends on it.** `aenopen`: reject `MODOPEN`/`CLONEOPEN` (`aen.c:107-108`); decode minor→`board_index`(bits0-3)+`sap_index`(bits4-7), sap_index 0 = pseudo-clone "next free SAP"; **rewrite `*devp` via `makedevice(...)` to encode the chosen sap back into the minor** (`aen.c:157`) — without this, the *second* open (ARP side) can't address the same stream; `EBUSY` if `aen->q` already set (`aen.c:161`). `aenclose`: null **both** `q->q_ptr` and `OTHERQ(q)->q_ptr` (`aen.c:200-201`); halt GEM on **last** close only. open/close run under the STREAMS perimeter (single-threaded per queue on UP 030) — no spinlock needed, but state that reliance.

**Persistent `/etc/inet` config:**
- `/etc/inet/network-config` — add (presence-gated, LITERAL IP):
  ```sh
  /usr/amiga/bin/zen -S &&
      /usr/sbin/slink addaen /dev/zen0 zen0 &&
          /usr/sbin/ifconfig zen0 192.168.2.39 netmask 255.255.255.0 up -trailers
  ```
  Ship a tiny `zen -S` presence tool backed by a board-count ioctl (on WinUAE it returns "no board" → boot doesn't hang). **Edit `S69inet`/`inetinit` in place — hardlink pair; never unlink/recreate, never park an `S*` backup in `rc2.d/`.**
- `/etc/inet/hosts` — add `192.168.2.39 amix-z3660 amix-z3660.alanara.fi`.
- `/etc/inet/rc.inet` — default route already present; keep.
- **Device node** persists on the on-disk root `/dev` — `mknod` once. No `strcf` change needed.

---

## 8. Risks & Open Questions (ranked; ★ = needs real HW; all source-answerable items front-loaded to Phase 0)

> **Status (2026-08-24): only Q5 (GEM RX filter mode / multicast policy) and Q6 (64-slot backlog overrun) are still open.** The list below is kept as ranked at design time, with each closed item marked where it was answered. Full history: `CHANGELOG.md`.

1. **★ Q-COHERENCY (highest):** Can `sptalloc` produce a CI PTE for the eth **DDR frame pages** (clean fix), or must the driver do explicit `cinvl`/`cpushl` per handshake? The draft's "window is CI" was **false** (TT0 `CI=0`) and "mirror z3660.c" **does not transfer** (sync+MMIO vs async). Resolve `sptalloc`/`PG_*` capability in Phase 0; **prove** on the real-HW coherency soak. *(Source-answerable for the mechanism; real-HW to confirm.)*  **→ RESOLVED — §10.1 + the shipped driver.** Per-page CI is impossible (no `PG_CI` bit on the 030), and the explicit ops turned out to be unnecessary: metal 2026-06-21 ran TX and RX byte-correct with no maintenance at all. The 030 driver ships none; the 040/060 line ops are compiled in but gated off (`z3660eth_cache = 0`). See `docs/BLIZZARD-F3.md`, CHANGELOG 2026-08-24.
2. **Q1 (interrupt wiring):** Firmware asserts **INT6** (confirmed). How does AMIX service a level-6 autovector — does it have/need an INT6 ISR table (it has no surveyed `int6_tbl[]`; aen/hydra/SCSI are all on `int2_tbl[]`)? *(Source-answerable: AMIX `ttrap.s` level-6 vector; real-HW to confirm.)*  **→ RESOLVED — §2.5 / §10.2, CHANGELOG 2026-07-30.** AMIX services level 6 but has no `int6_tbl` and no registration API; RX ships as a polled `timeout()` drain, so the interrupt question is decoupled from the datapath.
3. **★ Q4 (FCS/min-frame — the ARP-killer):** Three conventions disagree (`aen` `-CRC`, firmware raw `BdGetLength`, guest `-14`). Evidence points to **NO FCS in the delivered length** (firmware never calls `XEmacPs_SetOptions(FCS_STRIP)`; guest does no `-4`) → **do NOT subtract `ETH_CRC_LEN`; min frame 60 not 64.** **Confirm with a one-frame length probe** (a 64-byte-on-wire ARP → slot len 64 if FCS present, 60 if stripped) **before** wiring `toss_packet_up_stream`. *(Largely source-answerable; one real-HW length read to nail it.)*  **→ RESOLVED — the shipped driver does NOT subtract `ETH_CRC_LEN`** (`z3660eth.c:979` records the probe conditional that was never needed), and ARP, ping, telnet and ftp all work on metal.
4. **Q2 (TX flow-control model):** Synchronous TX with **no TX-done IRQ** → aen's defer-and-drain-from-IRQ has no trigger. Choose: (a) never-defer + TX-serialization lock, or (b) `putq` + explicit `qenable` after each blocking TX. Do not hold raised spl across the ≤1 ms store. *(Design-time; validate latency on real HW.)*  **→ RESOLVED — option (b), without a TX-done IRQ.** `q->q_first || !z3660ethxmit()` defers with `putq`; the deferred frames are drained by `z3660ethwsrv` and by `z3660eth_tx_drain()` off the same clock-level poll callout that drains RX, and a stream that cannot transmit ends ITS drain rather than the whole pass (`364333c`, 2026-07-26).
5. **★ Q3 (RX staging):** cached `rxbuff[]` vs direct CI-slot read — **moot until Q-COHERENCY pins the cache policy**; on a cacheable map both need the per-iteration invalidate.  **→ RESOLVED with Q-COHERENCY.** The shipped drain stages through the static `rxbuff[]` and issues no 030 maintenance; on the 040/060 line the window is cache-inhibited by `DTT0`.
6. **Q5 (multicast/promiscuous + RX filter mode):** No hardware filter register exists. Decide best-effort (rely on firmware GEM passing broadcast+own-MAC) vs NAK `DL_ENABMULTI`/`DL_PROMISCON`. **Also confirm the GEM's actual RX filter mode** — if it passes all traffic, AMIX IP must drop other hosts' unicasts. Moot for ARP+ping (broadcast flows); matters for multicast services and CPU waste.  **★ STILL OPEN.**
7. **Q6 (RX backlog overrun):** 64-slot ring overruns into the TX window (latent firmware bug). Mitigation = eager loop-drain (we do). Interacts with the drain model — a one-per-IRQ ISR + RX burst >32 lets the producer overrun. Flag for the coherency soak.  **★ STILL OPEN.**
8. **★ Q7 (single shared TX buffer):** One `txwin` — must serialize (ties to Q2a). Validate no two contexts race the buffer.  **→ RESOLVED — the single `txwin` is serialised by `info->tx_busy`** without holding raised spl across the ≤1 ms store (`z3660eth.c:814-838,882`).
9. **Q-INITMODEL (init wiring):** hydra-style `init_tbl` boot-probe (runs at boot on the GEM-less gate box, must be absent-safe) vs aen-style lazy-open (trivially safe for the Phase-1 gate). Lean lazy-open for Phase 1. *(Design-time.)*  **→ RESOLVED — lazy-open shipped** (no `init_tbl`/`io_init` entry, §10.3), which is what makes the GEM-less gate box boot cleanly and `open()` return ENXIO.
10. **Q8 (`/dev` minor convention):** confirm aen's actual node/minor on the golden image (`/tmp/amixgold` was I/O-locked; verify on-box `ls -l /dev`).  **→ RESOLVED — `/dev/zen0` = `c 51 0`** (`userland/S99zen`, `BUILD.md` §2(b)).
11. **Q-BUILD-ANCHORS:** pull live on-box `kernel.c`/`driver/Makefile`; verify `aen*` anchors exist; rewrite the clean-gate's touch/rm to full on-box paths. *(BLOCKER-1/BLOCKER-2 — Phase 0.)*  **→ RESOLVED — `tools/patch-kernel.sh` / `patch-makefile.sh`** anchor on `aen*`, are row-scoped and fail-closed, and are covered by the benchless `tools/patch-selftest.sh` (`603c719`, 2026-07-31); the net-aware clean gate is `../amix-kerntools/tools/build-clean-net-kernel.sh`.

---

## 9. Phased Implementation Plan

Deploy routine (LOCKED, two-tier): **build + clean-gate on WinUAE `.38`** → shuttle clean `relocunix` via `transfer.hdf` (`c5d0`, ~1 min) → **real box** `make install` + `/stand make` → reboot. Real-HW iface `192.168.2.39`; never re-image root. Keep the relocunix-vs-source shuttle variant *consistent* (§6.4).

**Phase 0 — Resolve source-answerable questions + de-risk the build (NO datapath build).**
- **Q-COHERENCY:** read on-box `immu.h`/`PG_*`; determine whether `sptalloc`'s `flag` can request a CI PTE for the eth DDR pages; decide CI-map vs explicit `cinvl`/`cpushl`. Read `z3660.c`'s mailbox handling only to confirm it is *not* a transferable precedent.
- **Q1:** AMIX level-6 autovector handling (`ttrap.s`) — which table the ISR registers in.
- **Q4:** confirm no `XEmacPs_SetOptions(FCS_STRIP)` in firmware (done — `ethernet.c` has none) and guest `-14`-only (done — `device.c:643`); plan the one-frame length probe.
- **Q-BUILD-ANCHORS (BLOCKER-1/2):** pull live `kernel.c` + `driver/Makefile`; verify `aen*` anchors; rewrite `build-clean-kernel.sh` touch/rm to full on-box paths covering `driver/z3660eth/{*.o,exp}`, `driver/exp`, `master.d/kernel.o`; refactor `driver.conf` parser to type-dispatch without regressing SCSI.
- **Q8 / Q-INITMODEL:** confirm free cdevsw major + golden `/dev` minor convention; decide lazy-open vs init_tbl.
- **Deliverable:** a 1-page "resolved contract" appendix fixing the IRQ table, the RX length/FCS boundary, the coherency mapping mechanism, and the verified build anchors. **Gate to Phase 1.**

**Phase 1 — Skeleton that compiles, links, boots, and ENXIOs (WinUAE-only).**
- Copy `hydra.c`→`z3660eth.c`, rename, keep all STREAMS/DLPI/`ifstats`/`toss_packet_up_stream` machinery **including DL_ATTACH/DL_DETACH** (the 7-primitive set) and the **clone/dev_t/EBUSY/dual-q_ptr** open/close logic. Stub the two seams (TX/RX return not-present). Write the autoconfig probe (reuse Z3660 SCSI discovery; **lazy-open model preferred** for gate safety). Copy the `ifstats` extern verbatim. Build the kerntools STREAMS path (§6.3) with the corrected anchors + gate.
- **Gate = boots without panic, registers in cdevsw, `open` returns ENXIO** (no board). Proves the whole integration/kerntools path independent of the datapath.
- **Milestone deliverable:** a clean-gated `relocunix` booting on WinUAE with `z3660eth` registered and ENXIO-ing — the kerntools STREAMS path proven end-to-end.

**Phase 2 — Fill the TX seam; first frame on real HW.**
- Implement `z3660ethxmit`: assemble flat frame (correct `u_short` SAP no-swap; pad per Q4/M10), `bcopy` to `txwin`, apply the chosen coherency mechanism (CI-map or `cpushl`-before-trigger), trigger `0x190`, read rc (rc==1 = no-carrier → queue, not error). Implement the chosen Q2 TX flow-control (lock-serialize the single `txwin`, no raised spl across the stall). Read MAC at probe.
- **Loop:** WinUAE compile/boot gate (TX still ENXIOs there) → shuttle to real box. **Real-HW test:** `slink addaen` + `ifconfig zen0 … up -trailers`; send broadcast ARP / crafted frame; observe on the wire.
- **Deliverable:** verified TX of a frame from real HW onto the wire.

**Phase 3 — Fill the RX seam; ARP + ping.**
- Implement `z3660ethintr` + corrected `receive_interrupt` (re-read `0x1A4` per strobe, serial-sentinel break, ack `24`, per-iteration cache invalidate, FCS-correct length, advance `0x194`), reuse `toss_packet_up_stream`. Wire the ISR per Q1.
- **Loop:** WinUAE compile/boot gate → real box. **Real-HW test:** `ping 192.168.2.1` (ARP must complete — Q4 boundary); bidirectional ping; default route.
- **Deliverable:** working ARP + ICMP both directions on real HW @ 192.168.2.39.

**Phase 4 — Coherency soak + hardening.**
- Sustained TX+RX under load with byte-verification (eth analogue of the SCSI `[WVERIFY]` campaign) — the only way to clear Q-COHERENCY, Q3, Q6, Q7. **Prove** the chosen cache mechanism. Resolve Q5 (multicast/promisc + GEM filter mode). Persistent `/etc/inet` config + `zen -S` tool. Tune spl/flow-control.
- **Deliverable:** a stable, persistent, reboot-surviving interface, soak-clean for coherency — ready to commit on `wip-ethernet` off `amix-base`. *(Superseded: the work shipped on `master`; the `wip-ethernet`/`amix-base` branch model was never used.)*

**Critical-path note:** Phases 2–4 are **real-HW-gated** (WinUAE has no GEM). Front-load every source-answerable question into Phase 0/1 so real-HW sessions (serial+KVM, no network until this works) are spent purely on the datapath and coherency, not integration mechanics.

---

### Sources (file:line via Surveys A–F + review verification)
Guest: `eth/device.c` (78-92,161-211,319,386,495-855,615-691,777-855,817), `common/z3660_regs.h` (42-93,278). Firmware: `rtg/rtg.c` (327-336,788-809,803-805,865-873,1150-1173,1609-1646), `ethernet.c` (59,510-668,540,553-561,596-639,1028-1095,1031-1034,1039-1040,546-548), `memorymap.h` (16,30-40), `interrupt.{c,h}` (c:29,50; h:19), `main.c` (644,683-692), `Z3660_emu/src/defines.h` (2-15). Skeleton/wiring: `hydra-amix/usr/sys/amiga/driver/aen/aen.c` (43,60-78,88-160,107,157,161,200-201,792,801,341,418,789-926,1168-1342,1212,1245,1270-1289,1357-1380), `.../hydra/hydra.c` (36,67-85,869,873-892,996-1015,1025-1047), `.../master.d/kernel.c` (38,61,185,187,193-194,265-266,342,357,397-398), `.../driver/Makefile`, `.../amiga/ml/ttrap.s` (327,329). Userland: `/mnt/etc/inet/{network-config,strcf:87-94,rc.inet:42,hosts}`, `/mnt/etc/init.d/inetinit`. Kerntools: `amix-kerntools/{build-kernel.sh:69-160, tools/build-clean-kernel.sh:27, README.md:21-22, templates/}`; SCSI contrast `amix-z3660scsi/{src/z3660.c:28-31,59,101-102,139-142,220-228,378-381, driver.conf, NOTES.md:48-50}`.

---

## 10. PHASE 0 RESOLUTIONS (2026-06-21 — source-confirmed against the on-disk Amix /usr/sys)

Two top open questions resolved against real kernel source (hydra-amix `/usr/sys` + amix-a4091 `tmp/amix-inc` headers). Both change the plan.

### 10.1 Q-COHERENCY — RESOLVED: per-page CI is impossible on the 030; the SHIPPED driver does no 030 cache maintenance (proven unnecessary on metal 2026-06-21), and the 040/060 line ops are compiled in but gated off by default.
- The board window (incl. eth windows `0x17ED0000`/`0x17EE0000`) is inside **TT0** (`ttrap.s:326-329`: `tt0=0x003F0143`, covers `0x0–0x3FFFFFFF`, **CI=0 ⇒ CACHEABLE**). On the 030 a **TT match BYPASSES the page tables**, so a PTE CI bit can't override it — and there is **no `PG_CI` bit** in this kernel (`immu.h:66-75` = only PG_M/REF/W/V). ⇒ **sptalloc-ing the eth pages cache-inhibited is IMPOSSIBLE** (the draft's "make it CI in Phase 0" path is dead).
  > **🔴 Mechanism correction (2026-08-13) — the conclusion stands, the reason was wrong.** AMIX does **not** use transparent translation at all: `amiga/boot/copyit.s:87-88` `pmove`s **zero** into `%tt0`/`%tt1`, and the `tt0_on` word `0x003F0143` has **E=0** — its own comment reads `…,Disable,CI=0`. What actually makes `0x0–0x3FFFFFFF` reachable is the **supervisor root table's section-0 entry**, a level-A **early-termination page descriptor** with page address `0` (built in `ml/exp` `pstart()`, `pstart+0xC8..0xFA`; `tc_on` gives TIA=2 ⇒ four 1 GB sections; only section 1 = `VSECT1 0x40000000` is table-backed, which is why `st_top1` exists and `st_top0` does not, and is what `sptalloc()` allocates from). Hence `immu.h:348` `phystokv(p) → (p)`. Cacheability is unchanged (CI clear in that descriptor), so **every conclusion in this bullet holds** — but the *threshold is a section boundary, not a TT range*, and the low 1 GB is reachable **by plain dereference**, which is what `z3660eth_map()` now does for a sub-`VSECT1` board.
- Stock `aen` does **ZERO** cache maintenance on its shared LANCE RAM (`aen.c:1032,1133,320-386`); coherent only because the 030 **data cache is effectively off** in validated configs (a4091: "data cache off / Amiberry cpu_data_cache=false"). An assumption, not a guarantee — and **unsafe to inherit for ASYNC RX**. `z3660.c` SCSI is **synchronous**, so its no-flush stance is NOT a valid precedent for async RX.
- **DECISION:** the driver issues explicit **68030 LINE-GRANULAR `cpushl dc,(an)`** looped over the frame's 16-byte lines — NOT `cpusha/cinva dc` (whole-cache op would discard unrelated dirty lines = data loss). TX: `cpushl` the TX frame after fill, before the `0x190` trigger. RX: `cpushl`/`cinvl` the slot's lines before reading header/payload. Correct whether DC is on or off → **no dependency on reading CACR**. ⚠️ verify the Amix `as` accepts `cpushl dc,(%a0)` syntax at build; provide as inline asm (kernel ships no cache helper).
  > **🔴 Shipping correction (2026-09-01) — the 030 ops in this DECISION were designed but never shipped.** `src/Makefile` never carried `-DZ3660ETH_CACHE_FLUSH` and none of the relink scripts injected it, so the block was compiled out of every kernel that has ever booted — and it was not missed: the 2026-06-21 metal bring-up ran TX and RX byte-correct with no maintenance at all. The reason is that the 030 data cache is effectively off in every validated config, and the Z3660 runs its guest 68030 on core1's UAE interpreter, which has no data cache to be stale. The dead `#ifdef` was removed on 2026-08-24 and replaced by the always-compiled, run-time-gated 040/060 maintenance described in the note below. Authority: `docs/BLIZZARD-F3.md` §0.
  >
  > **🔴 040/060 correction (2026-08-24, BLIZZARD F3 round 2 — see `docs/BLIZZARD-F3.md`).** Three parts of this bullet do not carry off the 030 line. (i) **"No `PG_CI` bit" is an 030 fact**: the 040/060 kernel writes a two-bit leaf `CM` (bits 6-5, `hat_cm_ram`, copyback since 2026-07-30), so the two mapping arms are *not* cache-equivalent there — the direct arm is cache-inhibited by `DTT0 = 0x003fc060` and the `sptalloc` arm would be copyback and **unusable** (a cached MMIO read fills a 16-byte line spanning four registers). (ii) **"No dependency on reading `CACR`" is false on the 68060**: `CPUSHL`'s invalidation is gated by `CACR.DPI`, so push-as-invalidate silently stops invalidating — which is exactly the defect the shipped `Z3660ETH_PUSH`/`Z3660ETH_INVAL` pair had, both being the same `cpushl`. `PUSH` and `INVAL` are now distinct, every push is followed by an explicit `CINVL`, and `CACR` is captured at attach. (iii) The syntax caveat is **settled**: the ops ship as raw `.word`s (`f468`/`f448`, encodings measured both directions against `as` 2.8.1), so no assembler-syntax dependency remains. All of it is gated on `z3660eth_cache` (0 = off, the shipping default) and disarmed unless the VA==PA identity is proven at attach.

### 10.2 Q1 (INT6 servicing) — RESOLVED: AMIX has NO level-6 driver hook → decouple the datapath from it.
- Only **INT2** has a driver table (`int2_tbl[]`, bare fn-pointers, shared-line self-poll; `kernel.c:188`, walked by `ttrap.s:40-49`). **Level 6 (EXTER) is hardwired** to `aciabintr` (CIA-B/clock; `ttrap.s:118-132`, `acia.c:42`) — no table, no hook.
- Firmware raises eth RX as **INT6/EXTER** by default; its INT2 path is gated behind `DEVF_INT2MODE` = a physical **Z3660 board-mod** (`device.c:233-242,309`).
- Three RX-delivery options, **none needing a firmware change**: (a) **polled** drain via STREAMS callout — zero IRQ wiring, ideal for datapath bring-up; (b) **chain** `z3660eth_drain()` into `aciabintr` (one C line, shares EXTER/clock); (c) proper **`int6_tbl`** + `p6int` walk in `ttrap.s` (clean, edits asm). The "cleanest" firmware-INT2-move needs a **hardware board-mod** → user decision, deferred.
- **DECISION:** write RX as a standalone `z3660eth_drain(board)` callable from EITHER a poll-callout or an ISR. Bring the datapath up with **(a) polled RX**; surface (b)/(c)/board-mod for interrupt-driven RX later. **Neither TX (synchronous) nor polled-RX needs the interrupt question solved.**

### 10.3 Knock-on simplifications
- **Firmware-change decision UNCHANGED (none needed)** — AMIX-side poll/chain delivers RX.
- **Phase-1 kernel.c edit = ONE insertion:** cdevsw slot **51** (`&z3660ethinfo`) + its extern, using `aen`'s **LAZY-OPEN** model (no `init_tbl`/`io_init` entry) → gate-safe on the GEM-less WinUAE box. No `int2_tbl`/`int6`/`acia.c` edits until Phase-3 interrupt-driven RX. Empty rows in the stock table confirmed at 48, 49 and 51–69 (`kernel.c:398-419`); the pick is **51**, because 48 and 49 carry community claims that the stock table cannot show — see §6.1.
- **Build anchors:** anchor kerntools edits on `aen*` (stock: cdevsw:357 `&aeninfo`, int2_tbl:193 `aenintr`); the golden box may lack hydra lines (BLOCKER-1) — still pull live `kernel.c` at gate time, but `aen` anchors are safe.
