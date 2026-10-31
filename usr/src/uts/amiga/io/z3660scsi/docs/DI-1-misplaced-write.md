# DI-1 — one 2 KiB piece of an inode block misplaced on the Z3660 SCSI path

**Analysis written 2026-10-03; §8 (state of the hunt) added 2026-10-03 evening; §9 (closing
evidence) added 2026-10-04. No shipping driver code was changed for it; bd8d900 and d78ac7f are
diagnostic cuts.** Tags: **measured** (read off an image, a disassembly or a source line,
cited), **derived** (arithmetic over measured facts, shown), **assumed** (neither; the check that
settles it is named).

## 1. Verdict

> **2026-10-04: §9 closes the hunt and supersedes §8.8's ranking.** The kernel's retry and
> error paths, the firmware's reset path and the host side are excluded, and the FTL candidate
> is weakened. The only sufficient mechanism is the firmware's register-value race (item 3).
> No load on a healthy bus reproduces it: 0 stale reads in ≈ 11.6 M register writes on the
> A4000D. The single event sits in the 2026-10-01 A4092 fault-hunt sessions. No driver change
> is owed (§9.8).

> **2026-10-03 evening: §8 supersedes items 3 and 5.** The rig (§7, run as kerntools E34) and
> metal have since cleared the kernel + driver request stream. The register race of item 3
> measured 0 stale reads in 4,165,877 register writes under load and drops to #2. The #1
> candidate is now the SD card's flash translation layer after an unclean power-down (§8.8).
> Items 2 and 4 stand, now also measured (§8.1–8.4).

1. **The damage is one 2048-byte request's worth of data at the wrong LBA, and it was on the
   disk before the last session booted.** The piece at +2048 of the inode block for inodes
   6656–6719 holds the piece from +4096. It is byte-identical to the master's +4096 piece except
   bytes 16–23 (`ic_atime`) of three dinodes. Those atimes belong to the **inode numbers**
   6675/6676/6677 (the rc scripts S16firstboot, S99zen, S20monsw). The 10-03 session stamped them
   after it had loaded the already-copied dinodes from disk. They are not a "newer in-core copy".
   The session's own writes of this block were correct. *(measured, §2.2–2.3)*
2. **On the shipping kernel an 8 KiB metadata write never reaches this driver as one request.**
   `gen_strategy` → `buf_breakup` sends it as four 2048-byte WRITEs at consecutive LBAs
   (34304, 34308, 34312, 34316 for this block). Every request lies inside one 4 KiB page. The
   driver passes each one straight to the mailbox. *(measured, §3, disassembly of b9cee294)*
3. **Ranked first: a register-value race below the driver** (Z3660 firmware/FPGA, cross-repo).
   The third request (LBA 34312, source +4096) ran with the *previous* request's block number
   (34308) in `WRITE_ADDR1`. This one capture error yields exactly the observed table. Piece 2
   (+2048) is overwritten with piece 3 (+4096). Piece 3's own LBA is not written in that pass
   (it holds identical old bytes). Nothing else changes. The Z3660 fork already measures this
   ("the FPGA raises the write event before its AXI write of the value lands", f1e8f40,
   `regrace_check()`). *(derived; the firmware confirmation is owed, §4.1)*
4. **Refuted: the page-crossing mechanism in `z3660_rw`.** It is refuted by the path (no request
   spans a page) and by the shape (a crossing garbles 4–6 KiB, never exactly one 2 KiB piece).
   Also refuted: cache maintenance (16-byte granular), the bounce path (unreachable), the
   completion FIFO, and any ZS-04 write-side analogue. *(§4.4–4.8)*
5. **Recommended first experiment** (§7): a rig run of the shipping kernel with the amiberry
   D245 write capture on. It runs a tagged raw-copy torture and a metadata churn. It clears or
   convicts the kernel+driver request stream byte for byte. The rig's mailbox is synchronous by
   value, so it cannot show the firmware race. The same guest script on metal, with the regrace
   firmware, is the decisive second step.

## 2. Evidence re-derived from the images

Reference: the card master `k1-desktop-rtg.hdf`, sha256 `403ca277902ae0ac…`. Damaged: the same
file pulled from the D-DEV card on 2026-10-03, sha256 `6ad5ba8fac65d516…`, crc32 59EA6326
(equal to the device CRC). Both are 209 715 200 B. Read with `amix-installng/tools/lib/ufs.py`,
read-only mmap.

### 2.1 Geometry (measured)

| fact | value |
|---|---|
| RDB | block 0; UNIX_Root sectors 128–342143 (UFS base byte 0x10000), UNIX_Swap 342144–403583, UNIX_Boot 403584–407679 |
| UFS | magic 0x011954, **bsize 8192, fsize 1024, frag 8**, ncg 132, ipg 512, fpg 1296, inopb 64, sblkno 16, cblkno 24, iblkno 32, dblkno 96, cgoffset 16, cgmask 0xFFFFFFF0, fsbtodb 1, optim SPACE |
| inode 6656 | cg 13, `itod` = frag 17088 → fs byte 0x10B0000 → **image byte 0x10C0000 = image sector 34304** (root-relative sector 34176) |
| the four 2 KiB pieces | sectors 34304 / 34308 / 34312 / 34316 = inodes 6656–6671 / 6672–6687 / 6688–6703 / 6704–6719 |
| fs_time | master 1992-03-25 05:42:25 (install); damaged 1992-03-25 08:59:52 (last clean unmount) — box clock |

The .hdf is the whole piscsi unit, so an image sector number is the LBA the driver puts in
`WRITE_ADDR1`.

### 2.2 The block, piece by piece (measured)

| piece | inodes | damaged vs master |
|---|---|---|
| 0 (+0) | 6656–6671 | differs only in ordinary atime updates (rc scripts run at the 10-03 boot) |
| 1 (+2048) | 6672–6687 | = **master piece 2** except 15 bytes: offsets 16–23 (`ic_atime`) of dinodes 6675, 6676, 6677 |
| 2 (+4096) | 6688–6703 | **byte-identical to the master** |
| 3 (+6144) | 6704–6719 | byte-identical to the master |

Consequences in the namespace (measured): 6672–6674 (symlinks under /usr/net/nls) and 6675–6677
(S16firstboot, S99zen, S20monsw) now carry the dinodes of bkhtofile, bkid, bkmadd, bkmsg,
bkregdisp and bkregvals. 6683 (the directory /usr/sadm/bkup/bin) is now a regular file. 6678–6682
are no longer free. Hence fsck's DUPs on I=6688–6693 and the bench symptom (S20monsw = an ELF,
S99zen = bkregdisp).

### 2.3 When it happened — the atime bytes date it (measured + derived)

The 15 differing bytes are the atimes of inode **numbers** 6675/6676/6677:

| inode | name in the directory | atime written (box clock) |
|---|---|---|
| 6675 | /etc/rc2.d/S16firstboot | 08:48:02 |
| 6677 | /etc/rc2.d/S20monsw | 08:53:58 |
| 6676 | /etc/rc2.d/S99zen | 08:54:41 |

Clustering every post-install timestamp in the damaged image gives the session map. The 10-03
10:24→10:36 session is box 08:47:47 (o.log) → 08:59:52 (fs_time): mount 08:47:58, rc activity
from 08:48:00, login 08:52:30 (lastlog), the SEC-1 chmods 08:56, shutdown 08:59:3x. The last
first-boot artefacts (firstboot.conf ctime 08:45:22, /var/adm/firstboot 08:45:45,
/etc/inet/hosts 08:46:08) belong to the 10-02 20:47→20:50 session.

So the 10-03 boot executed "S16firstboot" at 08:48:02 from a dinode that already pointed at
bkmsg's data. Its atime was stamped on that copied dinode, and `iupdat` wrote back the whole
copied dinode with only the times changed. S20monsw and S99zen got their atimes about a minute
after login, which is consistent with the operator inspecting them. **Therefore:**

- the copy was on disk (or in the boot's first read) before box 08:48:02 on 10-03;
- piece 2 then held exactly the master bytes, so **the source of the copy cannot be told apart
  as in-core or on-disk**. The "newer time fields ⇒ in-core" reading does not hold;
- the 10-03 session's writes of this block were **correct** 4×2 KiB writes. They carried the
  damage plus the atime updates, and piece 2/3 were rewritten with unchanged content;
- the damaging event follows the 10-02 20:47 boot, which still ran the first-boot dialog from
  the intact S16firstboot. The most likely event is a write of this block during that session or
  its halt flush. *(assumed: the box-clock-to-session mapping. The bench console frames
  settle it.)*

### 2.4 Block-wise diff classification (measured)

814 differing sectors, all inside UNIX_Root. The RDB, swap and boot partitions are
byte-identical. The full run listing is in `DI-1-sector-diff.txt`.

| class | sectors | reading |
|---|---:|---|
| inode blocks | 273 | per dinode: times-only updates, alloc/free, and runtime content changes (wtmp/wtmpx, mnttab, nodename, profile, vfstab, saf pids, firstboot files, /dev/zen0 reusing /dev/syscon's slot, /etc/shadow rewritten so 2144 now holds syslog.pid, /tmp recreated in root.shadow's slot). **The only non-runtime pattern is the 16 dinodes of §2.2.** |
| cylinder-group headers | 163 | allocation maps and summaries |
| data, newly allocated | 199 | wtmpx/wtmp/utmpx/lastlog/cron log/.xinitrc and similar |
| data, reallocated | 62 | freed and reused fragments |
| data, freed | 45 | fragments of since-removed files |
| data, free in both | 42 | stale bytes of temp files created and removed |
| data, same owner | 30 | directories and small config files edited at runtime |

**Neighbour-copy scan.** Every changed 2 KiB piece was compared with the pieces ±2/4/6 KiB away,
in both images. One true copy was found: the DI-1 piece. There were five near-matches, all
explained: zero-dominated small-file fragments (nodename, syslog.pid, a /var/saf directory) and
/dev inode blocks whose dinodes share one template. This damage is a single event.

## 3. How an 8 KiB inode-block write reaches the mailbox on KD060-Z (measured)

Disassembly is of the shipping kernel `KD060-Z-c0-u6`, sha256 `b9cee2946bd2784d…` (the RT07c
twin is byte-identical). Addresses are ELF `.text` offsets. Its z3660 object is 53f43bd = HEAD
minus the ZS-04 guard; `z3660_rw` is unchanged since.

1. **`bwrite` → `bdevsw[].d_strategy` = `gen_strategy`.** `fix_swtbls` @0x3d684 copies every
   block major's strategy into `shadowbsw` and installs `gen_strategy` (all configured majors use
   `nullflag`; amix-kernel-analysis `vm-map/BIO-PFN-PHYS-KVA-CENSUS.md`). Buffer-cache data comes
   from `kmem_zalloc(bsize)` (`bio.c` getfreeblk); no PFN arithmetic is involved.
2. **`gen_strategy` @0x3d988:** for `b_bcount` ≤ 4096 it passes the buf to the real strategy.
   For more than 4096 with `b_pages == NULL` (any buffer-cache buf) it calls
   `buf_breakup(gen_strategy, bp)` (@0x3da0e).
3. **`buf_breakup` @0x3d17a** (Model-B patched first chunk, stock loop):
   - first chunk `d2 = min(bcount, 4096 − (b_addr & 4095))` (@0x3d1b4–0x3d1d0). If that is a
     whole page (> 4095) it skips straight into the loop (@0x3d1da);
   - loop chunk `d2 = min(resid, 0x800)`. `pea 0x800` @0x3d21e is the stock 2 KiB constant,
     deliberately left unpatched (port `patch_modelb.py` note);
   - each chunk is a copy of the buf with `b_bcount = d2`, through `gen_strategy` (≤ 4096 →
     `ddstrategy`). The loop waits for B_DONE, then `b_blkno += (d2+511)>>9` (@0x3d278) and
     `b_addr += d2` (@0x3d288).
   - **So every request lies inside one 4 KiB page, whatever the buffer's alignment.** An 8 KiB
     buffer at a page boundary becomes (34304, A), (34308, A+2048), (34312, A+4096),
     (34316, A+6144).
4. **`ddstrategy` @0xbe84 → `startio` @0xc0c8:** one `sdcom` per buf. The block comes from
   `sdblkno(bp)`, `addr = vtop(b_addr, b_proc)`, the CDB is WRITE(10), count = `b_bcount>>9`,
   nbyte = `b_bcount`. Both the LBA and the address are read from the same buf in one call. The
   dd completion-order shims are active: @0xc0ae → `dd_issue_first`, @0xc0b8 →
   `dd_startio_tail`.
5. **`z3660queue` → `z3660_rw` @0xf6ec0:** one chunk (≤ 64 KiB, so one for 2 KiB). The WRITE
   arm is `z3660_push`, then `+0x240 ← block`, `+0x244 ← len`, `+0x248 ← pa`, `+0x000 ← unit`
   (@0xf6f64–0xf6f70), then return. **Nothing is read after a WRITE doorbell.** The READ arm
   writes `+0x20/+0x24/+0x28`, rings `+0x04`, then reads `USED_DMA` (+0x9C).
6. **Raw path** (fsck, mkfs, tunefs, clri, dd to /dev/rdsk): `physio` → dd `breakup` @0xbe6a →
   `amiga_dma_pageio` @0xdba8 uses one `ngeteblk(0x800)` bounce for the whole transfer.
   Chunk = `min(resid, 0x800)`, `b_blkno += (d2+511)>>9`, `b_addr += d2`. So it is also 4×2 KiB
   at consecutive LBAs, with the **same** `pa` in every request.

The 040/060 port's reading ("ddstrategy queues the buf whole → one 16-block request") misses
steps 1–3. On this kernel the block path and the raw path both issue 2 KiB requests.

## 4. Ranked mechanisms

The ranking below is the analysis as first written. **§8 supersedes it.** The evening status:

| rank (first written) | mechanism | 2026-10-03 evening (§8) |
|---|---|---|
| #1 (§4.1) | register-value race on the mailbox (firmware/FPGA) | **#2**: structural and real, but 0 stale first reads in 4,165,877 register writes under RTG + RAM load on the A4000D; the as-shipped arm is pending (§8.4, §8.8) |
| #2 (§4.2) | kernel pairs one chunk's LBA with another's address | cleared on the rig: replay byte for byte, every sector carries its own tag (§8.2) |
| #3 (§4.3) | stale file pointer after a failed `f_lseek` | excluded: 0 SCSI ERROR/refused hits in every hdf0 serial segment, PSTAT err=0; FatFs excluded by reading (§8.7) |
| #4 (§4.4) | page crossing in `z3660_rw` | refuted, now measured: 0→0 on the rig and on metal; the -CD build's 11 are RDB reads, harmless (§8.3, §8.4) |
| #5–#8 (§4.5–4.8) | cache, bounce, completion FIFO, ZS-04 | refuted (unchanged); rig counters 0 (§8.2) |
| new | host upload path | excluded (§8.6) |
| **new #1** | the SD card's FTL after an unclean power-down | **ranked first**; the decisive probe is pending a scratch card (§8.8) |

**2026-10-04: §9.8 replaces this table.** The register race is the only sufficient mechanism,
not reproduced on a healthy bus. The SD-card FTL is weakened. The kernel's retry path, the
completion-order shims and the firmware's reset path are excluded (§9.1, §9.2, §9.5).

### 4.1 #1 — register-value race on the mailbox's block register (Z3660 firmware/FPGA)

**Mechanism.** On the real-CPU path the 060's register write lands in a memory cell at
`RTG_BASE + zaddr` and raises an event. The ARM takes the event and reads the value **from the
cell at service time**: `rtg.c` ~602–646, `zdata = swap32(*address)` →
`handle_piscsi_reg_write()`. `scsi.c` then latches `piscsi_u32_write[i] = val`
(@1702–1707). The WRITE doorbell seeks with `piscsi_u32_write[0]` (@1553–1557) and sources
`f_write` from `piscsi_u32_write[2]` (@1593–1620). If the event is serviced before the AXI write
of the new value lands, the latch keeps **the previous value written to that register**, i.e.
the previous request's.

**Which stale register gives which picture** (derived). For a run of consecutive 2 KiB writes k
= 0..3, LBA L+4k, source A+2048k:

| register captured stale in request k | effect | visible? |
|---|---|---|
| `WRITE_ADDR1` (block) | piece k's data written at LBA of piece k−1; piece k's LBA keeps old bytes | **yes — DI-1 = k=2** |
| `WRITE_ADDR3` (address) | piece k's LBA gets piece k−1's data (opposite direction) | yes (block path); invisible on the raw path (same bounce `pa`) |
| `WRITE_ADDR2` (length) | none while every request is 2048 | no |
| `DRVNUMX`, the doorbell value (unit) | none (always unit 6) | no |
| `READ_ADDR1` / `READ_ADDR3` | in-core piece k = disk piece k−1 (or lands one piece back); only reaches disk if written back, and then in the **opposite** direction to DI-1 | — |

Exactly one capture fits DI-1: request k=2's block register. A later-read variant fits too: the
address of request k read after request k+1 wrote it. It needs the FPGA to post at least four of
the 060's writes ahead of the ARM, and only the firmware log can tell the two variants apart. A
read-side event cannot produce DI-1's direction under either variant. So DI-1 is a **write**
event: one 8 KiB metadata write in which the third 2 KiB request landed one piece low.

**Driver's share.** The driver writes block → len → address → doorbell with nothing in between
and nothing after a WRITE doorbell. Nothing in the driver detects or prevents a stale latch. The
READ arm's `USED_DMA` read serializes completion, but it cannot undo a value latched stale at
event time.

**Confirm / refute.** On metal with f1e8f40: `[REGRACE] reg 0x240 …` lines (and 0x248, 0x020,
0x028) during a write-heavy run. The §7 tag script on metal then finds displaced pieces whose
direction matches the stale register. Refuted if displaced pieces keep appearing over a long
torture while REGRACE stays at zero stale first reads.

**Predictions.**
- The installng prevalence sweep should look for **both** directions ("piece k-1 = copy of
  piece k" and "piece k = copy of piece k-1") at 2 KiB granularity in metadata blocks.
- The B_PAGEIO path (`gen_strategy` multi-page, 4 KiB per request) gives the same pictures at
  4 KiB granularity.
- f1e8f40's `regrace_check` returns the re-read value, so a fork build carrying it already
  masks the stale-first-read variant. Which firmware the D-DEV card ran matters: the pull log's
  boot menu reads as 1.03 BETA 21.

### 4.2 #2 — the kernel pairs one chunk's LBA with another chunk's address

Refuted statically. `startio` reads `b_blkno` (via `sdblkno`) and `b_addr` (via `vtop`) from
the same buf in one call. The z3660 transfer happens inside that call, so the pair is
snapshotted. `buf_breakup` / `amiga_dma_pageio` advance both fields together, in process
context, after B_DONE. Interleaving would need `startio` to run from an interrupt between
@0x3d284 and @0x3d288, and even that gives (new LBA, old address): disk piece k ← memory piece
k−1, the opposite direction. **Residual:** kernel memory aliasing, i.e. another 2 KiB buffer
overlapping this buffer's +2048 slice (the Model-B 2 KiB/4 KiB history). That is port-owned, and
§7's capture convicts it if it is real.

### 4.3 #3 — firmware stale file pointer after a failed `f_lseek`

Stock firmware continues a request after a failed seek; the fork refuses it (`scsi.c` comment
@1308–1312). The write then lands at the previous request's end. For that to be +2048 →
displaced, the previous request must have ended at LBA 34312, which needs a duplicated or
reordered request. Ranked low. The serial log and the fast-seek (link map) state on the card
settle it.

### 4.4 #4 — page crossing in `z3660_rw` (the coordinator's candidate) — refuted

- **Path:** §3. No buffer-cache request above 4096 B reaches the driver unsplit, and every
  split piece lies inside one 4 KiB page. Identity-mapped buffers (below 0x40000000, DTT0) are
  physically linear anyway.
- **Shape** (derived). Suppose an 8 KiB request were sent whole from scattered 4 KiB frames:

  | buffer start | pieces sourced correctly | pieces sourced from the next physical frame |
  |---|---|---|
  | page-aligned | 0, 1 | 2, 3 |
  | page offset 2048 | 0 | 1, 2, 3 |

  Neither gives "piece 1 wrong, pieces 2–3 right". It would also take the next physical frame
  to hold this same block's other half, which is not credible.
- `z3660_pagecross_n` **is in the shipping object**: the test `(pa & 4095) + len > 4096`
  @0xf6f12–0xf6f24 uses 4 KiB on this line. By §3 it should read 0. A non-zero value means a
  ≤ 4096-byte non-pageio buffer that is not page-aligned (see §6.2), not DI-1.

### 4.5 #5 — data-cache maintenance — refuted

The shipping kernel has `z3660_cache` initialised to 0x3C (60) in `.data`, so CPUSHL+CINVL run
over [pa, pa+len) before each WRITE. Any cache error is 16-byte-line granular. A whole, exactly
aligned 2 KiB copy of another piece is not a cache signature.

### 4.6 #6 — bounce / shared static buffer — refuted

Driver staging fires only for `pa < 0x08000000`. AMIX RAM starts at 0x08000000, so it is
unreachable (`z3660_bounce_wr_n` counts it). The firmware sources from its own bounce only for
addresses outside its DDR window (`scsi.c` @1597–1600); that would garble every write, not one.
The raw path's single bounce is per-transfer, waits for B_DONE per chunk, and the transfer is
synchronous.

### 4.7 #7 — completion FIFO, duplicate issue, interrupt races — refuted

The data moves at issue time with a snapshotted (LBA, address) pair (§4.2). The FIFO only orders
the delivery of `ihandle`. A duplicate issue re-sends an identical pair. `z3660_cq_overflow` and
`z3660_nest_hits` are the in-kernel checks (§5).

### 4.8 #8 — ZS-04 / a 2048-byte unit block size — refuted

There is no shared block-size variable. `bs` comes from `z3660_geom[unit]` per command, and is
512 for unit 6. ZS-04 refuses `blocks > nbyte/bs`, which dd.c never produces on a 512-byte disk,
so the guard would not have fired. A wrong `bs` would misplace data ×4 in LBA and length, not
move one piece. The write-side analogue does not exist (a CD write is rejected with DATA
PROTECT before any doorbell). The shipping object lacking ZS-04 is irrelevant to DI-1.

## 5. Observables in the shipping kernel (measured, `KD060-Z-c0-u6.map`)

Runtime address = load base + offset. The map header measures the base as 0x08000000 on the
Z3660 060. **Verify it first:** `z3660_cache` must read 0x0000003C. Read with an lseek+read tool
(`zz9k_memprobe -q`), never `dd skip=`. All are BSS, so they cover the current boot only, not
the DI-1 session.

| symbol | offset | expected |
|---|---|---|
| `z3660_cache` (D) | 0x11dd50 | 0x3C (base check) |
| `z3660_pagecross_n` | 0x13e820 | 0 (§4.4) |
| `z3660_bounce_wr_n` | 0x1503e0 | 0 |
| `z3660_bounce_rd_n` | 0x13e578 | 0 |
| `z3660_cq_overflow` | 0x1417cc | 0 |
| `z3660_nest_hits` | 0x13d6d0 | 0 |
| `z3660_lastblock` / `z3660_lastlen` | 0x165c84 / 0x149a5c | last request |

The shipping object has no record of *which* chunk crossed. Driver objects built from source
after 2026-10-03 add `z3660_pagecross_ring`, one entry per counted crossing (unit, LBA, len, pa,
request base, R/W, seq). The layout and the decode recipe are in `docs/PAGECROSS-RING.md`.

## 6. Fix specs (no code in this round)

### 6.1 Driver defence against a stale register latch (relevant to DI-1)

The real fix belongs to the firmware/FPGA (latch after the AXI write lands, or carry the value
in the event). As defence in depth the driver can make every request self-checking:

- After writing `ADDR1/2/3` (READ or WRITE triple), **read each back** before the doorbell. The
  read handler returns the latched `piscsi_u32_*[i]` (`scsi.c` @2092–2096). On a mismatch,
  rewrite that register, re-read, and count `z3660_regrace_n`. After N retries, fail the request
  (EIO, no doorbell).
- After a **WRITE** doorbell, read one register (e.g. `P_DRVNUM`). This serializes completion
  like the READ arm's `USED_DMA` read.
- Keep the ZS-04 guard and the order DRVNUMX → ADDR1 → ADDR2 → ADDR3 → doorbell.
- **Cost:** 3 extra reads before the doorbell and 1 after a WRITE, against a 2 KiB SD write plus
  the existing 5 accesses. Measure it with the driver's existing per-request timing on the rig
  and on metal rather than assume it.
- **Tests:**
  - host harness: give `mock_regs.h` a "stale first latch" mode. Gates: the mismatch is detected
    and corrected, the counter moves, the retry bound fails the request, and the normal path's
    byte stream is unchanged;
  - rig: an amiberry fault-injection knob that latches the previous value with probability p
    (amiberry repo), plus the §7 tag torture with and without the read-back;
  - metal: REGRACE plus the tag torture.
- **This must not be read as papering over the firmware.** The counter makes the firmware defect
  visible, and the firmware fix remains owed.

### 6.2 Page splitting (asked for; not the DI-1 fix)

The driver **cannot** split at page boundaries. `sdcom.addr` is a physical address by contract
(NOTES 2026-07-11 (a)), and with no virtual address there is nothing to `vtop()` the next page
from. A driver bounce cannot gather the source pages either. The guarantee has to come from the
issuer, and on this kernel it does for every buf above 4096 B (§3). The remaining gap is a
≤ 4096-byte non-pageio buffer that is not page-aligned and not identity-mapped. Spec:

- keep `z3660_pagecross_n` (§5) as the gate;
- if it is ever non-zero, add a small kmem ring {block, len, pa} for crossing chunks and find
  the issuer;
- the fix then goes at the issuer (port repo): extend `gen_strategy`'s single-page test
  @0x3d994 to `(b_addr & 0xfff) + bcount ≤ 4096`, else route through `buf_breakup`;
- ZS-04 is kept. The hot path costs nothing.

## 7. Recommended first experiment — the rig, shipping kernel, D245 capture

**Run 2026-10-03 as kerntools row E34 (792cdd8): clean. Results in §8.2.**

**Goal.** Prove or convict the kernel + driver request stream (§4.2/4.4/4.6/4.7) byte for byte,
with the instrument that will later read metal. The amiberry mailbox applies register writes by
value and runs the doorbell synchronously (`src/z3660_scsi.cpp` ~397). **A clean rig therefore
moves DI-1 below the driver; it does not reproduce #1.**

**Host preparation** (a durable scratch directory on the host):
1. `root6.hdf` = a copy of the master (sha256 `403ca277…`, verify); keep `root6.pre.hdf` too.
   Its UNIX_Boot carries KD060-Z b9cee294, the kernel under test.
2. `di1scratch.hdf`, 48 MiB in three areas: A (sectors 0–32767), B (32768–65535, tags),
   C (65536–98303). Area B sector s = `b"DI1TAG\0\0" + pack(">II", s, 0xD11D1100)` + 496 bytes
   `(s*7+i) & 0xFF`. A and C start zeroed. Keep `di1scratch.pre.hdf`.
3. Config: the K1-desktop piscsi rig shape used for the cut-4 rows (kerntools
   `rig/matrix.tsv`), root on unit 6, plus
   `uaehfN=hdf,rw,DH5:/…/di1scratch.hdf,32,1,2,512,0,,scsi5_z3660` (unit 5 → `/dev/rdsk/c5d0s0`,
   well under the stock-sddevsize 603 MB edge).
4. Environment: `AMIBERRY_D245_LOG=<dir>/cap`, `AMIBERRY_D245_LOG_UNITS=di1` (matches both
   paths when the root copy is named `di1root6.hdf`), `AMIBERRY_D245_LOG_READS=hash`. Bring it
   up through `piscsi-rig.sh up` in a held slot.

**Guest script** (root shell on the console; pre-POSIX sh):
```
# raw path: 4 x 2 KiB requests per 8 KiB, consecutive LBAs, one bounce pa
R=/dev/rdsk/c5d0s0
dd if=$R of=$R bs=8k skip=2048 seek=0    count=2048    # B -> A
for p in 1 2 3; do
  dd if=$R of=$R bs=8k skip=0    seek=4096 count=2048  # A -> C
  dd if=$R of=$R bs=8k skip=4096 seek=0    count=2048  # C -> A
done
# block path: buf_breakup 4 x 2 KiB per inode block, the DI-1 path on the DI-1 layout
mkdir /var/tmp/di1; i=1
while [ $i -le 512 ]; do dd if=/dev/zero of=/var/tmp/di1/f$i bs=$i count=1 2>/dev/null; i=`expr $i + 1`; done
r=0; while [ $r -lt 30 ]; do touch /var/tmp/di1/*; sync; sleep 1; r=`expr $r + 1`; done
# counters (section 5), then a clean halt from the console root shell
sync; sync; init 0
```

**Host analysis:**
1. `amiberry/tools/d245-writelog.py verify cap --before *.pre.hdf --after *.hdf` for each unit:
   the emulator stored what it was told.
2. Tag scan of `di1scratch.hdf`: every sector of A and C must carry tag index (its offset within
   the area). Report mismatches per 2 KiB piece, with direction.
3. Capture scan: for every WRITE record to unit 5, each 512-byte sector's tag must equal LBA −
   area base. For root writes, `region --offset` over the inode blocks of /var/tmp/di1 (from
   `ufs.py`), and every 2 KiB piece must decode as dinodes whose number matches the directory.
4. Root: `ufscheck.py` reports 0 DUP fragments, and `/var/tmp/di1/fN` has size N.
5. §5 counters as expected.

**Reading the result.** All clean → the kernel and driver issue only consistent (LBA, address)
pairs on this path. Run the identical guest script on the A4000D with the firmware side's
regrace firmware: REGRACE lines plus tag mismatches there decide #1. Any rig mismatch → the
capture names the exact request (sequence number, LBA, length, payload), and the hunt moves to
the kernel (port) or this driver accordingly.

## 8. State of the hunt, 2026-10-03 evening

Commit hashes cited here and in §9 from other repos (the kerntools rig, Z3660 diagnostic branches,
the 040/060 port, installng) point to unpublished diagnostic work.

**Added 2026-10-03 evening. Docs only; no driver code changed.** The sources are internal bench
reports (not published) from the firmware side, the 040/060 port, kerntools and the A4000D bench.
Each point names the source and, where one exists, the commit. In this section **measured** means
measured by the named source and reported there; this repo did not re-run it. **This section supersedes §1 items 3 and 5 and the ranking in §4.**

**In one paragraph.** The kernel splits every block-path metadata write into four 2 KiB requests,
confirmed by the port against the source. The rig replays the kernel + driver request stream
byte for byte on both kernels, so DI-1 is below the driver. On metal the driver's page-cross
counter stays at 0, and the register race of §4.1 did not occur once in 4,165,877 register
writes under the heaviest A4000D load. The host upload path and FatFs are excluded. The #1
candidate is now the SD card's flash translation layer (FTL) after an unclean power-down. The
decisive probe needs a scratch card.

### 8.1 The kernel breakup is confirmed (§3)

The 040/060 port confirmed §3 against the usl-svr42 source (`predki.c`, `bio.c`; port 22e565e,
DRIVER-LINE-5 amended) *(measured)*:
- `fix_swtbls` installs `gen_strategy` on dd (`d_flag` = `nullflag`);
- a buf of ≤ 4096 B passes whole;
- `buf_breakup` sends the first piece to the page edge (converted). Every later piece is
  `min(resid, 0x800)`, unconverted, at `.text` 0x3d21e;
- on a page-aligned 8 KiB buffer the first piece would be a whole 4096 B page, so it is skipped
  and the 2 KiB loop starts at the beginning: exactly **four 2 KiB requests**.

So **every block-path metadata write is four back-to-back 2 KiB requests at consecutive LBAs**.
DI-1's exposure needs no raw writer. That matches the bench logs: no repairing fsck in 522 OCR'd
console frames, and no raw writer in any hdf0 session. The port's earlier one-request reading
(§3, last paragraph) is withdrawn by the port. Its 1.1 note: 0x800 → 0x1000 at 0x3d21e halves
the requests (each still inside one page); the raw `amiga_dma_pageio` pair changes only together
with a page-aligned `ngeteblk(4096)`.

### 8.2 The rig: the request stream is consistent (§7 run)

kerntools 792cdd8, rows E32/E32b/E33/E33b/E34, on the shipping KD060-Z b9cee294 and on
KD060-Z-CD 94fbb4fd *(measured)*:

| row | run | result |
|---|---|---|
| E32 / E32b | metadata torture, 12,000 files, 26 min per kernel | 0 DI-1 signatures, 0 DUP fragments |
| E33 / E33b | raw path, tagged patterns, bs 8k and 2k | 4096/4096 pieces correct |
| E34 | §7 as written: D245 capture, 48 MiB tagged scratch, 512-file churn | the write-log replay reproduces the post images byte for byte; all 57,344 scratch writes were 2 KiB requests; every sector carries its own LBA's tag |
| all | §5 counters, in-run | 0 (`z3660_pagecross_n` 0→0 on the shipping kernel; 11→11 boot-time on -CD, §8.3) |

**Reading.** The kernel + driver request stream is consistent on both kernels, for the block and
the raw path: every 2 KiB request lands with its own tag. §4.2 (LBA/address pairing), §4.4, §4.6
and §4.7 are now cleared by measurement, not only by reading, and §4.2's residual (buffer
aliasing) did not show. As §7 said, the rig's mailbox is synchronous by value. This result moves
DI-1 **below the driver** on the rig. It says nothing about the firmware race or the card.

### 8.3 The page-cross counter question is closed

- **The 11 boot-time crossings on KD060-Z-CD** are exactly the 11 × 512 B RDB reads (4 + 3 + 4
  per window) into `getrdb()`'s static `block` *(measured: port verdict by arithmetic on
  kerntools' windows; bench evidence, not published)*. cdfs's growth moved that buffer to page
  offset 0xE24 (VA 0x08136E24). On the shipping kernel it sits at 0x63C and does not cross.
  Below 0x40000000 is the DTT0 identity region (`vtop040` returns phys == va), so the two
  "pages" are consecutive physical memory and the transfer is correct. The counter is pure `pa`
  arithmetic, so this is **counter noise from layout**. No fragment read and no 4 KiB transfer
  crossed. No 1.0 fix; the 1.1 cleanup is to 512-align `block`.
- **Refinement of §4.4 and §6.2.** A crossing is a hazard only for a buffer at VA ≥ 0x40000000
  (kvseg) whose second page is not the next frame. The pa-only `z3660_pagecross_n` cannot make
  that distinction. The port's allocator argument (usl-svr42 `bio.c`:456 `kmem_zalloc` and the
  SVR4.0 buddy kma: a buffer of ≤ 4096 B is size-aligned inside one page) says no fragment or
  4 KiB buffer can straddle. So the offset-aware `gen_strategy` test of §6.2 is not a 1.0 item.
- **Census.** A bench-only census pass exists in the port (b2b9732 `ddcensus`, never shipped).
  dd `startio`'s `vtop` goes through `dd_vtop_census`, which counts identity crossers
  (`dcn_cross_id`), kvseg crossers (`dcn_cross_kv`) and **`dcn_kv_scat`, the real hazard** (a
  kvseg crosser whose second page is not the next frame), with a 16-entry ring. `dcn_kv_scat`
  is 0 so far; kerntools runs the diagnostic chain over the matrix for the measured statement.
  This repo's `z3660_pagecross_ring` (bd8d900, `docs/PAGECROSS-RING.md`) is in that diagnostic
  chain. Neither ships.

### 8.4 Metal, A4000D

*(measured)*
- **Page-cross counter.** `z3660_pagecross_n` 0 → 0 across a 6-cycle metadata torture (6,018
  files, `fsck -n` clean) on the shipping kernel (hdf6, r3 desktop). The driver page-cross is
  excluded on metal too.
- **Regrace cell (interim).** The diagnostic firmware F583F05B (Z3660 `diag-regrace` 61bb936)
  carries a per-register stale-read counter: it logs the first and the second read of every
  register write, keeps a heartbeat, and has a console `RGR` toggle between the masked and the
  as-shipped value. Setup: bench OS preset 6, RTG on with rtgload drawing 1024×768×16 (28,048
  frames/h) plus RAMLOAD, scratch unit 2, netdd 0.4 arm A (back-to-back 2 KiB writes, every
  sector tagged). Results:
  - **0 stale first reads in 4,165,877 SCSI register writes**;
  - four masked passes of 102,396 back-to-back 2 KiB writes: 409,584 sectors ok each,
    **0 misplaced**, 0 stale, 0 corrupt;
  - PSTAT short=0 err=0.
- **Pending:** arm A as shipped (`RGR`), and arms Q (queued), R (random), B (8 KiB) and C
  (64 KiB). The bench reading: if those stay at zero, the race is not DI-1's cause.

*(derived)* The counter compares the two reads in either mode, so "0 stale first reads" means
the race never happened in those passes; masking had nothing to mask. The as-shipped arm tests
the consequence directly: a misplaced piece would carry the previous request's tag.

### 8.5 Metal, A4000T (firmware side)

*(measured; internal bench report)*
- Shipped firmware 719AF9B7, bench OS + RAMLOAD, **RTG off**: one full arm-A pass (102,396
  back-to-back 2 KiB writes) was **CLEAN**: 409,584/409,584 sectors, misplaced 0, stale 0,
  corrupt 0, no tag missing (image A07223DA).
- **RTG on** (rtgload 1024×768×16 at ~74 fps + RAMLOAD): the 060 froze **before the first write**
  reached the firmware. This is a known freeze class: on that box RTG drawing plus piscsi I/O freezes
  the 060 at once on the shipped firmware, while RTG off survives 100k requests. The loaded case
  is untestable there, and the A4000T is closed for DI-1 (card == master, box off).
- The firmware side's reading suggests the freeze sits on the same register/bus handshake as the race, if it
  holds over more samples (N=1 per arm). Open; tracked separately.

### 8.6 Host side excluded (kerntools)

*(measured, by reading the tools and the firmware)*
- Scripted uploads (`deploy-image-kvm.sh`, `deploy-kvm.sh`) are gated on device CRC == host
  CRC before any swap. The firmware's CRC command re-reads the file from the SD
  (`sd_fileops.c`:351–412).
- `http_upload` is one sequential `curl -T` PUT. The firmware handler `f_write`s the pbufs in TCP
  order with no seek (`lwip/httpd.c`:383–407).
- **hdf0 (k1-desktop-rtg.hdf) was never uploaded to D-DEV.** It arrived in the sha256-verified
  2026-10-01 card write (`make-card.sh`: rsync, sync, unmount, remount read-only, then sha256 of
  every file against the MANIFEST).

So **the damage happened on the card after 2026-10-01**: through an Amix session on the
firmware's FatFs write path, or in the SD card itself.

### 8.7 FatFs excluded (firmware side)

*(measured, by reading ff.c R0.15)* `csect` is recomputed from `fptr` per sector. The cluster is
re-resolved from the CLMT at every boundary. Direct writes are clipped at the cluster edge, and
the refill touches only the FIL buffer. The 2026-08 CLMT truncation bug is fixed on
`amix-main-2`, which is in 719AF9B7. Every hdf on the card today is 1 fragment. With the bench
serial logs (0 "SCSI ERROR"/refused/bad-write hits in every hdf0 segment, every PSTAT short=0
err=0), this also closes §4.3 (a stale file pointer after a failed `f_lseek`).

### 8.8 New #1 candidate: the SD card's FTL after an unclean power-down

*(the ranking is the firmware side's; the FTL itself is not observable, so the mechanism stays
assumed until probe d)*

**Mechanism.** A consumer SD card's FTL maps logical ranges to physical flash pages. If power is
cut while the card is still committing writes it has already acknowledged, the map can end up
with one logical 2 KiB range pointing at the physical page that holds a **newer write of the
neighbouring range**. Reading the logical range then returns the neighbour's data. The firmware
issues no flush or "wait until committed" step after its writes, and the bench cut power seconds
after the halt banner. D-DEV has had many hard power-offs. A clean halt protects the filesystem,
not the card's FTL.

**Ranking now** (firmware side, 22:30):
1. the SD card's FTL after an unclean power-down;
2. the register race of §4.1. It remains a real structural defect: the FPGA raises the ARM event
   in the same clock as it starts the AXI write of the value (z3660.v:713–716 against
   :1249–1265), and the fix belongs to the FPGA (event after `bvalid`). But it measured 0 in
   4.17 M register writes under load (§8.4);
3. FatFs: excluded (§8.7).

**Probes** (firmware side):
- a. 5× CRC re-read of the damaged hdf: stable = a persisted mis-mapping; varying = a failing
  card;
- b. a fresh 200 MB pattern file, 10 CRC re-reads over an hour, and one power cycle with no
  writes;
- c. the card's CID, manufacturer and date from a host reader when the card is out;
- d. **decisive**, on a **scratch SD card only**: write load, then a power cut a few seconds
  after the halt, ×10, host-checked each time. Pending a scratch card (an open release decision).

**Bench rule now:** ≥ 10 s between the halt banner and any power cut, on both benches.

### 8.9 The dating in §2.3 fits an FTL fault as well (derived)

§2.3 found that the damaged piece is the **master's** +4096 bytes, with only the atimes of inode
numbers 6675–6677 restamped by the 10-03 session. It is not a "newer in-core copy". The copy was
on disk before box 08:48:02 on 10-03, and the 10-03 session's own writes of the block were
correct.

That reading needs no in-core source, so it fits an FTL mapping fault as well as the register
race:
- Piece 3 (+4096, inodes 6688–6703, the bk* tools) is byte-identical to the master in the
  damaged image (§2.2), so every write of it, in the 10-02 20:47→20:50 session too, carried
  master bytes. A mis-map of logical +2048 onto the physical page of such a write gives exactly
  "+2048 = master +4096, +4096 correct", with nothing else changed.
- The window fits too: after that session's last write of the block (its halt flush) and before
  the 10-03 boot read it. A power cut seconds after that halt is the FTL event. *(assumed: the
  power-cut timing after that halt; the POWER-LOG settles it.)*
- After the fault, the 10-03 session read the wrong piece, stamped three atimes on it, and wrote
  the block back as four correct 2 KiB requests, which the card stored correctly.

Both mechanisms give the same image. They differ in when the fault happens (the race: during a
write, under load; the FTL: at a power loss after acknowledged writes). The prevalence sweep
(installng 5eae517: 1 positive out of 167 images, the A4000D pull only) fits both. Probe d and
the A4000D's as-shipped arm (§8.4) tell them apart.

### 8.10 What changes in §1–§7

- §1 items 2 and 4 stand, now also measured (§8.1–8.4). Item 3: the race drops to #2. Item 5:
  the rig step is done (§8.2) and the metal step is under way (§8.4).
- §4.1, "which firmware": D-DEV ran the fork's 719AF9B7 during the 10-02 20:47 session ("1.03
  BETA 21" is the fork's banner too; firmware side). `regrace_check` exists only on the
  `diag-regrace` branch (f1e8f40, 61bb936), which never lands. So nothing masked the race on
  D-DEV.
- §6.1: the firmware side holds that a driver read-back goes through the FPGA read path, not the
  ARM's DDR view, so it cannot prove what the ARM latched (internal bench report: "no driver
  mitigation exists"). This contradicts §6.1's reading of `scsi.c` @2092–2096 and is unresolved. It is not
  pursued while the race measures zero.
- §6.2: the ring was added (bd8d900). The issuer-side `gen_strategy` test is not a 1.0 item
  (§8.3).
- **No driver change is owed for DI-1 as of this evening.**
- **Diagnostic only, added later on 10-03:** a driver-side fault knob for the rig's
  kernel-retry cells (REFUSE, or SHIFT the block register; −4 on the 3rd piece gives the §2.2
  picture). See `docs/FAULT-KNOB.md`. It is not a DI-1 fix and never ships.

## 9. Closing evidence, 2026-10-04

**Added 2026-10-04. Docs only; no driver code changed.** The sources are internal bench reports
(not published): the DI-1 verdict, compiled 2026-10-04 00:20 and updated as the A4000D cell
finished, and the release work plan. As in §8, **measured** means measured by the named source
and reported there; this
repo did not re-run it unless a commit or file of this repo is named. **This section supersedes
§8.8's ranking and closes the hunt from the driver's side.**

**In one paragraph.** Source reading and experiment now agree: the kernel never pairs one
piece's LBA with the next piece's data, on the normal path or on the error path. A driver-side
fault knob on the rig produces DI-1's picture only when the block register is shifted. It also
showed that the kernel writes every inode block twice per sync. On the A4000D every netdd
torture arm stayed clean under RTG drawing and RAM load, with 0 stale register reads in about
11.6 M register writes. The firmware's reset path is excluded and the FTL candidate is weakened.
What remains is the firmware's register-value race: sufficient, structural, and not reproducible
on a healthy bus. The one event happened during the 2026-10-01 A4092 fault-hunt sessions. No
driver change is owed.

### 9.1 The kernel cannot misplace a piece (040/060 port)

*(measured, by reading the shipping binary and the usl-svr42 source; internal bench report,
row "Kernel retry / completion-order shims")*
- **No kernel path pairs piece k's LBA with piece k+1's data.**
- **`buf_breakup`'s per-piece advance window has the opposite orientation.** The loop stores
  `b_blkno` (@0x3d284) and then `b_addr` (@0x3d288). A request built between the two stores
  would carry (piece k+1's LBA, piece k's address): disk piece k+1 ← memory piece k. DI-1 is
  disk piece k ← memory piece k+1 (§4.2 derived the same orientation).
- **Nothing can issue that header inside the window.** A header is reused only after `biodone`,
  and the Z3660's completions are either synchronous or drained in `z3660_complete`.
- **dd.c's retry path** (`ihandle` → `startio` with RETRY) rebuilds `com.addr` and the CDB from
  the same head buf, so a retry re-sends a consistent pair. It is also unreachable on the Z3660
  for data I/O: the driver never returns a status ≠ 0 with `okay` TRUE for a disk READ or WRITE.
  *(This repo's reading of `src/z3660.c` agrees: the only okay-TRUE non-zero status is the
  CD-ROM write's DATA PROTECT, which never reaches the medium. Every other refusal is status
  0xff with `okay` FALSE.)*
- **The dd completion-order shims** (`dd_issue_first`, `dd_startio_tail`; §3 step 4) only
  reorder delivery. They neither build nor change an (LBA, address) pair.

### 9.2 The rig fault-knob cells (kerntools E36/E36b/E36c)

*(measured; kerntools d27e142, `rig/matrix.tsv` rows E36/E36b/E36c; recipe and host tools are
bench evidence, not published)*

The kernel is the diagnostic chain KD060-Z-FK-c0-u6 (c9c6cb64): the shipping KD060-Z recipe
plus this repo's d78ac7f. That object carries bd8d900's page-cross ring and the one-shot WRITE
fault knob (`docs/FAULT-KNOB.md`). The D245 capture is on. The host creates
`/var/tmp/di1x/f1..f64` and arms the knob on one piece of f33's 8 KiB inode block (L = 225920).
It then rewrites the block (touch + sync) and checks the capture.

| cell | knob | result |
|---|---|---|
| E36 | REFUSE on piece 2 (LBA L+4) | the faulted flush wrote piece 1 only. The refused piece reached no LBA, and pieces 3/4 of that pass were not issued. The next flush wrote all four pieces at their own LBAs. The D245 replay is exact (2763 writes) and the post image is clean. **The kernel error path never misplaces**, which confirms §9.1 by experiment. |
| E36b | SHIFT −4 on piece 3 (LBA L+8) | piece 3's payload was written at L+4 (seq 5108): **DI-1's picture, in the write record**. A second flush of the same block 75 ms later rewrote all four pieces correctly, so the post image is healed. Replay exact (2766 writes). |
| E36c | as E36b, single-inode touch; the damaged state reconstructed | the kernel still flushed the block twice, ~70 ms apart (seq 5111–5114 faulted, 5160–5163 healing). Replaying the capture through seq 5113 (verified exact) gives the disk right after the faulted write. Piece 2 equals piece 3 except later-stamped dinode times: **hdf0's picture exactly** (§2.2). |

**New fact: the kernel flushes an inode block twice per sync**, about 70–75 ms apart and with
identical content, even for a single `touch; sync` *(measured, E36b/E36c)*. Consequence
*(derived)*: a stale-ADDR1 hit on the first flush heals itself at the second. Only a hit on a
block's **last** write persists, so a persisting DI-1 is rarer than the raw race rate.

**Detectors must compare time-masked** *(measured)*. The reconstructed piece 2 matches piece 3
only with the dinode time fields masked, as in hdf0 (§2.2: the `ic_atime` bytes of three dinodes
differ). An exact-copy detector misses it. kerntools' `di1sig.py`
(bench tooling, not published) now compares exact and time-masked windows over allocated
dinodes. It reports 1 finding on the reconstruction, 0 on the master, and 0 on every DI-1 rig
image re-checked. installng's `tools/fsdupcheck.py` compares the block-pointer arrays (dinode
bytes 40–99), so it is time-insensitive. Its self-test for a re-stamped copy is installng
6d30d96. **The 167-image prevalence sweep (5eae517) stands at 1 positive**, the A4000D pull.

### 9.3 Metal, A4000D: every torture arm clean

*(measured; internal bench report; diagnostic firmware F583F05B = Z3660
`diag-regrace` 61bb936)*

The setup is §8.4's: bench OS preset 6, RTG on with rtgload drawing 1024×768×16, RAMLOAD,
scratch unit 2, netdd 0.4 TORTURE (every sector tagged, two read-backs). hdf0's file on the card
was contiguous (46 × "[FASTSEEK 6] … 1 fragment").

| arm | requests per pass | mode | read-back |
|---|---:|---|---|
| A, back-to-back 2 KiB | 102,396 | masked, 4 passes (first boot) | 409,584 sectors ok per pass |
| A | 102,396 | as shipped (`RGR`), 4 passes | 409,584 ok per pass, both read-backs |
| Q, SendIO-queued 2 KiB | 102,396 | as shipped | 409,584 ok |
| R, random-order 2 KiB | 102,396 | as shipped | 409,584 ok |
| B, 8 KiB | 25,599 | as shipped | 409,584 ok |
| C, 64 KiB | 3,200 | as shipped | 409,584 ok |

- **Every read-back: 0 misplaced, 0 stale, 0 corrupt.**
- **[REGRACE]: 0 stale first reads** in 4,165,877 register writes (first boot) plus 7,445,677
  (second boot), so 11,611,554 ≈ **11.6 M piscsi register writes** across both boots (the sum
  is derived).
- **No 060 freeze on the A4000D.** There was one ping miss during arm R while the firmware kept
  logging. The halt was clean, 16 s before the power cut.

*(derived)* That is 12 passes × 409,584 sectors written and read back, with 1,023,960 of the
writes as 2 KiB requests (A ×8, Q, R). The counter compares both reads in either mode, so arm A
masked already showed that the race did not happen. The as-shipped arms test the consequence
directly: a misplaced piece would carry the previous request's tag. None did.

### 9.4 Metal, A4000T: unloaded clean, loaded untestable (firmware side)

*(measured; §8.5)* On the shipped firmware 719AF9B7 with RTG off, one full
arm-A pass was clean: 409,584 of 409,584 sectors. With RTG drawing, piscsi I/O froze the 060
before the first write, so the loaded case cannot be tested on that box. The freeze is tracked
separately.

### 9.5 The firmware's reset path is excluded (firmware side)

*(measured, by reading the fork firmware)*
- **Writes are atomic in the cooperative core0 loop.** A WRITE's `f_lseek` and its one
  `f_write` complete before `reset_thread` runs (`scsi.c`:1530–1663). The reset interrupt only
  sets a flag (`interrupt.c`:68–80; `main.c`:711–738).
- **No doorbell follows a real-060 reset.** The registers persist, but the 060 is held in CPLD
  reset.
- **"Refreshed drives" only reads.** It re-parses the RDBs: no close, no sync, no CLMT rebuild,
  no writes.

So a reset inside a run of four 2 KiB writes cannot split or re-aim one of them in the firmware.

### 9.6 When it happened (bench serial audit; kerntools)

*(measured)*
- hdf0 (k1-desktop-rtg.hdf) arrived in the sha256-verified 2026-10-01 card write (27 of 27
  files verified). It was **never uploaded over HTTP**: the 10-02 "deploys" were dry runs of
  other files (§8.6 and the bench serial audit).
- The only abrupt stops after writes to hdf0 are two **unlogged** sessions on 2026-10-01, at
  **17:12** and **17:18**, during the A4092 card-1 fault hunt. A defective Zorro III bus-master
  card in the slot held the bus. Amix was reset ("Reset active (DOWN)" + "Refreshed drives")
  2–12 s after hdf0 writes, with no I/O after it. Power was cut about 1 minute later. Every
  later session halted cleanly.
- The DI-1 verdict report dates the damaged copy before the 10-02 boots (`ic_atime` stamps).

**Amends §2.3.** §2.3's dating stands: the copy was on disk before box 08:48:02 of the 10-03
session. Its last bullet placed the event in or after the 10-02 20:47 session, on an *assumed*
box-clock-to-session mapping. The serial audit and the verdict's dating supersede that placement. This
repo did not re-derive the box-clock mapping.

### 9.7 The SD-card FTL variant is weakened

*(derived from §9.6)* §8.8's mechanism needs a power loss while the card is still committing
writes it has acknowledged. In both 10-01 stops the card stayed powered for about a minute after
the reset. Every later session halted cleanly. So the FTL is no longer ranked first. It is not
excluded either, because the FTL is not observable.

**Pending** (firmware side, §8.8):
- probe a: 5× CRC re-read of the damaged hdf (stable = a persisted mis-mapping; varying = a
  failing card);
- probe b: a fresh 200 MB pattern file, 10 CRC re-reads over an hour, and one power cycle with
  no writes.

Probes c (the card's CID) and d (the scratch-card power-cut series) stay as listed in
§8.8.

### 9.8 Conclusion

- **The sufficient mechanism is the firmware's register-value race** (§4.1: a stale
  `WRITE_ADDR1` on the third request). It is structural and real: the FPGA raises the ARM event
  in the same clock as it starts the AXI write of the value (z3660.v:713–716 against
  :1249–1265). The fixes belong to the Z3660 repo. The real fix is in the FPGA (raise the event
  after `bvalid`, on the deferred bitstream list). The stopgap is the firmware re-read until two
  reads agree. Both are upstream PR candidates; the re-read exists only on `diag-regrace`, not
  in the shipped fork firmware.
- **It is not reproducible on a healthy bus** under any load we can generate. On the A4000D it
  gave 0 stale first reads in ≈ 11.6 M register writes and 0 misplaced sectors in every arm
  (§9.3). The A4000T was clean at RTG off (§9.4). The rig was clean too (§8.2), but its mailbox
  is synchronous by value.
- **The single event sits in the abnormal-bus environment** of the 2026-10-01 A4092 card-1 fault
  hunt (§9.6).
- **No driver change is owed.** A page split is impossible in the driver (§6.2), and a register
  read-back cannot prove the ARM's view (§8.10). §6.1 is not pursued.
- **The shipping z3660scsi object stays e072408 (ZS-04).** The DI-1 kernel b9cee294 carries the
  older 53f43bd (§3); DI-1 gives no reason to change either. This repo's bd8d900 (page-cross
  ring) and d78ac7f (fault knob) are diagnostic cuts only and never ship.
- Whether this clears DI-1 as a freeze blocker is an open release decision.

**Final status of every candidate** (replaces the table in §4 and the ranking in §8.8):

| mechanism | final status | where |
|---|---|---|
| register-value race on the mailbox (firmware/FPGA) | **the only sufficient mechanism**; not reproduced on a healthy bus | §4.1, §8.4, §9.3, §9.4 |
| kernel request stream, retry path, completion-order shims | excluded (source and rig) | §8.1, §8.2, §9.1, §9.2 |
| page crossing in `z3660_rw` | refuted, measured 0; census E35 `dcn_kv_scat` 0 over 132,231 requests (kerntools 5de893b) | §4.4, §8.3, §8.4 |
| cache, bounce, completion FIFO, ZS-04 | refuted | §4.5–4.8 |
| stale file pointer, FatFs | excluded | §8.7 |
| host upload path | excluded | §8.6 |
| firmware reset path | excluded | §9.5 |
| SD-card FTL after a power cut | weakened; probes a/b pending | §8.8, §9.7 |

**What changes in §1–§8.** §1 item 3 stands as the only sufficient mechanism, with the rider
that it does not reproduce on a healthy bus. §1 item 5's metal step is done (§9.3). §2.3's
placement of the event is superseded (§9.6). Any prevalence detector built on §4.1's predictions
must compare time-masked dinodes or block pointers (§9.2). §8.8's FTL ranking drops to weakened
(§9.7).
