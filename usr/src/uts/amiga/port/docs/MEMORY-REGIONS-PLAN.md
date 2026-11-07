# More RAM for AMIX: non-contiguous memory regions (analysis, 2026-11-06)

Status: step 1 built and boot-tested 2026-11-07 (relink-040-memregions.sh); step 2 (memspan, 272 MB) built and boot-tested 2026-11-07, not installed; regions above the kernel not yet. Base kernel: `chain-z3660-a3000.sh` output
(`build/z3l/*-ksg`, /stand/unix since 2026-11-06), which boots at 256 MB after the
`ksegtbl` fix.

## 1. Where we are

AMIX keeps physical memory in one range, as in 1991:

| variable | set by | meaning |
|---|---|---|
| `MAINSTORE`, `VSIZOFMEM` | `config()` (.text 0x18f5c) from `bootinfo.memory[]` (bootinfo+0x440) | the ONE region that contains the kernel |
| `physmem`, `maxclick` | `mlsetup()` | `maxclick = btopr(MAINSTORE) + physmem` (4 KB clicks, Model B) |
| `pages`, `epages` | `page_init()` from `kvm_init()` | the page_t array, 60 bytes per 4 KB page, in kvseg |
| `pages_base`, `pages_end` | `page_init()` | pfn range the array describes |

Every pfn <-> page_t conversion is plain arithmetic:
`pp = pages + (pfn - pages_base) * 60`, `pfn = (pp - pages) / 60 + pages_base`,
and "is this RAM?" is `pages_base <= pfn < pages_end`.

The loader already hands the kernel a list of regions (`bootinfo.memory[]`); `config()`
just keeps the one the kernel sits in and drops the rest.

### How many places know the layout (z3-shm, relocations into .text)

| symbol | refs | functions |
|---|---|---|
| `pages` | 90 | 60 |
| `pages_base` | 99 | 55 |
| `pages_end` | 43 | 34 |
| `epages` | 33 | 20 |
| `MAINSTORE` / `VSIZOFMEM` | 17 / 10 | config, mlsetup, kvm_init, memstart, xclosef |
| `maxclick` / `maxmem` / `physmem` | 6 / 3 / 2 | mlsetup, kvm_init, main |

About half of the `pages_base`/`pages_end` uses are in our own 040/060 HAT sources
(`src/hat040.s`, `ptest040.s`, `prfastmap040.s`, `bp_map040.s`, `cb_release040.s`,
`hat_dup040.s`, `legacysdt040.s`, `i10rev040.s`, `pageinitzero.s`); the rest are stock
C in the binary: `vm_page.c` (page_get/free/lookup/hashin/reclaim/abort/...),
`pageout`, `setupclock` (clock hand), `memialloc`, `segkmem_*`, `pagecopy`, `pagezero`,
`ppcopy`, `gen_strategy`, `prmapout` (/proc), `segu_softunload`, RFS (`rfc_*`, `rfesb_fbread`).
`phystopp`/`pptophys`/`ptob` are dead (no callers; they still use the 030's `<<11`).

## 2. The techniques, old and new

| technique | where | idea | pfn -> page_t cost |
|---|---|---|---|
| one flat array | AMIX today, early Linux FLATMEM | one array for one range | arithmetic |
| flat array with holes | Linux FLATMEM + `PageReserved` | one array over the whole span; hole entries marked reserved, never freed | arithmetic |
| **memseg list** | SVR4.2, Solaris | one `struct memseg` (own page array, pfn range) per region; `page_numtopp` walks the list | list walk (Solaris adds a last-hit cache and later a hash) |
| sparse vmemmap | Linux SPARSEMEM_VMEMMAP (today's default) | page array virtually contiguous over the whole span, only the parts describing real RAM are backed; `pfn_valid()` from a section bitmap | arithmetic |
| memblock | Linux boot allocator | region list from firmware, early allocations from it, then handed to the page allocator | n/a |
| zones / NUMA nodes | Linux, Solaris lgroups | slow and fast memory kept apart, fast used first | n/a |
| highmem / kmap | Linux 32-bit, Solaris kpm (the reverse) | RAM the kernel cannot reach 1:1 is user-only; the kernel maps a page for a moment when it must touch it | extra mapping per touch |

What fits AMIX: AMIX is a binary kernel we relink. A real **memseg list** means changing
all ~265 sites above; 130 of them are in stock C we can only reach with wrappers or binary
patches. That is the Solaris design, but the wrong cost for us.

**The 2026 answer is Linux's: keep the arithmetic, change what the array covers.**
`pp = pages + (pfn - pages_base)` stays valid everywhere if the array spans all regions
and the entries for holes exist but are never usable. That is "flat array with holes"
for small holes and "vmemmap" for big ones. Then the only code that must change is the
code that builds and walks the array, plus the "is this RAM" tests.

## 3. What memory is there to get

Z3660 DDR map (firmware, see memory notes on the Z3660 firmware):

| DDR / 060 address | use |
|---|---|
| 0x01000000-0x07FFFFFF | firmware core 0 heap |
| 0x07000000-0x07FFFFFF (A3000 board, not DDR) | A3000 motherboard Fast RAM, 16 MB, slow (A3000 bus) |
| 0x08000000-0x17FFFFFF | 060 RAM, 256 MB, AMIX today |
| 0x18000000-0x1FFFFFFF | RTG |
| 0x20000000-0x2FFFFFFF | **looks unused, 256 MB** |
| 0x30000000-0x3FFFFFFF | core 1 (image + heap) |
| 0x40000000+ | Zorro III boards (AmigaOS allocation), collides with AMIX kernel VA |

Measured 2026-11-07 (step 1 kernel, native boot at 256 MB):

```
memregion 0: 1000-1FFFFF chip
memregion 1: 7000000-7FFFFFF RAM, not used yet
memregion 2: 8000000-17FFFFFF RAM, kernel (MAINSTORE)
memregion 3: 40000000-4FFFFFFF above 1 GB, not reachable
```

Region 3 is the Z3660's extra 256 MB, offered to AmigaOS as a Zorro III board
(manufacturer 5195, product 2) at 0x40000000: exactly the start of AMIX's kernel
virtual region 1.  Region 1 now meets region 2 without a gap (MemHeader rounding).

So the realistic targets, cheapest first:

1. **272 MB, contiguous:** motherboard 0x07000000 + CPU RAM 0x08000000-0x17FFFFFF.
2. **512 MB, one 128 MB hole:** 0x08000000-0x17FFFFFF + 0x20000000-0x2FFFFFFF, IF the
   firmware lets the 060 address DDR 0x20000000-0x2FFFFFFF (shanshe's `cpu_ram2` bitstream
   work is this kind of change; unverified that it can map this range).
3. **>1 GB / Zorro III RAM at 0x40000000+:** needs highmem or a kernel VA move (section 6).

## 4. Plan

### Step 0: firmware and loader (no kernel risk)
- Decide with shanshe whether 0x20000000-0x2FFFFFFF can be 060 RAM (core 1 stays at
  0x30000000; check nothing else, e.g. SMUSH codec buffers, uses 0x2xxxxxxx).
- pvmk passes `bootinfo.memory[]` with only regions below 0x40000000, each tagged with a
  speed class (DDR vs motherboard), sorted by address. pvmk memtests every region.

### Step 1: region list in the kernel ("memblock")
`config040.s` already wraps `config()`. After `config_orig`, a new `memregions_init`
copies `bootinfo.memory[]` into a kernel table `memregion[8] = {base, end, class}`
(only regions below 0x40000000, page aligned) and sets `MAINSTORE` = lowest base,
`VSIZOFMEM` = highest end - lowest base (the span). This also retires the 1991
`0xF7C00000`/`0x08000000` fallback (KNOWN-ISSUES, "MAINSTORE fallback").
Also exports `pfn_valid(pfn)` (table walk, 1-8 entries) for the HAT.

### Step 2: findings from the kernel (2026-11-07)

Read from the step 1 kernel (mrg-shm). These change the step 2 design below:

- **The array does not start at MAINSTORE.** `kvm_init` (0x48e18-0x48ebc, inline) sizes
  the array as `(maxclick - ff) * 60` + hash, takes its physical pages at `ff` (the first
  free page after the kernel, ksegmappt and ksegupt) with `sptalloc(n, 1, ff, 0)`, then
  `pages_base = ff + n`, `page_init(pp, maxclick - pages_base, pages_base)`,
  `memialloc(pages_base, maxclick)`. RAM below the kernel can only join if that block
  is replaced (a `jmp` to our own tail, which returns to 0x48ebc). Extending
  MAINSTORE/VSIZOFMEM alone does nothing for the array.
- **The kernel image then falls inside the array** (0x08000000 .. pages_base), and with
  it the static page tables `ksegmappt`/`ksegupt`. `hat_pt2ptdat` (stock, 18 callers) uses
  `pages_base <= pfn < pages_end` to tell "VM page holding page tables" from "one of the
  static kernel tables" (else it panics). With a spanning array the static tables
  would take the VM-page branch. So **every range check has to become a
  "managed page" test**: pfn in [region base, region end) for regions that were freed,
  excluding kernel image, static tables, the array itself and holes. `pfn_valid` from
  step 1 is "is RAM", not "is managed": step 2 adds `pfn_managed`.
- **How many range checks** (relocations to pages_base/pages_end, by pattern):

  | code | `cmpl pages_base,%dN; bcs; cmpl pages_end,%dN; bcc/bcs` | other forms |
  |---|---|---|
  | stock (13 functions: hat_init, hat_devload, hat_pt2ptdat, hat_ptalloc, hat_sdtfree, hat_swapout, page_numtookpp, page_numtouserpp, prfastmapout, prmapout, segu_softunload) | 14 | 8 base + 8 end (mostly the end compare against %d0, hat_ptalloc 3, hat_swapout 2) |
  | ours (src/*.s, 12 functions) | 12 | ~8, edited in source |

  The 14 stock pairs can be patched mechanically (20 bytes -> `jsr pfnchk_dN; bcs; nops`,
  a helper per register that keeps all registers and answers in the carry flag); the 16
  odd stock sites need reading one by one. The ~48 `subl/addl pages_base` uses are pure
  pfn<->pp arithmetic and stay.
- **Whole-array walkers** (stock, via `epages`): setupclock (6), pageout (4), fsflush (2),
  free_vp_pages, page_* asserts. Kernel-image/hole entries keep page_init's state
  (p_lock set, no vnode); each walker must be checked to skip such an entry.
- **Motherboard RAM speed** (`dd` from /dev/mem, 8 MB, kernel time, 5 runs): DDR 1.0 s,
  motherboard 1.3-1.4 s. Copy-loop bound, so the real gap is larger. Free its pages
  first in memialloc (page_free with dontneed puts at the head, so the last freed, DDR,
  is handed out first).
- **Cost/benefit:** the machinery (kvm_init tail, pfn_managed, ~30 stock patch sites,
  ~20 source sites, walker checks) is the same for 272 MB (+16 MB slow RAM) and for 512 MB
  (+256 MB DDR, needs the firmware to place it below 0x40000000). 272 MB is the test
  vehicle; the payoff is 512 MB.

### Step 2: test results (2026-11-07, msp-shm, native boots over serial)

- `-m #128`: total 150994944 (144 MB = 128 + 16), available 145842176; 4 x 32 MB
  patterns held and verified (needs motherboard pages), no bus errors, fsck -n clean.
- `-m #256`: total 285208576 (272 MB), available 278028288; managed ranges read live
  from /dev/mem: [0x7000,0x8000) and [0x85ef,0x18000), pages_base 0x7000; no leak
  (maxmem 0x10a11, availrmem 0x103cf, freemem 0xd1d5 after the tests).
- Parallel pressure (8 python processes) fails with MemoryError at 0-12 MB each while
  ~160 MB is free; the installed ksg-shm (no memspan) at 256 MB fails the same test the
  same way, so it is an older limit (ISSUE-39/40 family), investigated separately.
- Monitors (amixsysmon, sar) read symbol addresses from the /stand kernel with the
  same build id: test kernels need their own build id (and a copy in /stand) to show
  right values.

### Step 2: flat array with holes (design as first planned)
- `kvm_init`/`page_init` already size the array from `maxclick`; with the span in
  `MAINSTORE`/`VSIZOFMEM` it covers the holes automatically. Cost of the 128 MB hole in
  target 2: 32768 x 60 = 1.9 MB of page_t, 0.4 % of 512 MB. kvseg for 512 MB:
  page array 9.4 MB + hash, so build with KVSEG_MB=24 or 32 (ksegtbl covers 1 GB now).
- `memialloc` (and the first-free loop) wrapped so it frees only pfns where
  `pfn_valid`; hole entries get the same state as kernel-owned pages (locked, never on
  the free list, never hashed). Then page_get/page_free/hash code never sees them.
- Walkers of the whole array must skip holes: `setupclock`/`pageout` clock hand
  (pages..epages), `page_init`'s loop. They already skip locked kernel pages; check
  each by disassembly that a hole entry looks exactly like one, else wrap them.
- **Hazard:** "is this RAM" = `pages_base <= pfn < pages_end` becomes wrong for pfns in a
  hole: the HAT would treat RTG (0x18000000-0x1FFFFFFF, inside the span) as RAM and
  touch its page_t. Fix in our sources: `Lpt_inram` (ptest040.s), hat040.s,
  prfastmap040.s, bp_map040.s ... use `pfn_valid`. Stock C users of the range test
  (`page_numtookpp`, `page_numtouserpp`, `hat_devload` path, `/proc`) get wrappers.
  This list is the bulk of the work and the main risk.
- Slow RAM (motherboard): put its pages on the free list first so they are handed out
  last (page_free puts to the head?, check `page_sortadd`), or keep it out until measured.
  Linux would make it a separate zone. Measure 060 bandwidth to 0x07000000 vs DDR first.
- DMA: ZS-05 SCSI bounce-buffers below 0x08000000 (fine, just slower); ethernet and
  core 1 rings must be checked for buffers in the new regions (core 1 reads 060 RAM
  through its own mapping: the new region must be in that mapping too).

### Step 2b: the Z3 RAM above the kernel (2026-11-07, 528 MB)

pvmk's `z3reloc` (Z3660 firmware AMIX mode) moves the Z3 RAM to 0x20000000 and the RTG to
0x30000000 and passes the new bases in bootinfo; memregions then sees region 3 as RAM.
`mspan_build` widens the array to the end of the highest RAM region above the kernel's
(`mb_above`), frees it after the motherboard RAM and before the kernel region, records it
as managed, and sets `maxclick` to the array's end. Built on `a3k-kv32-ser` (array ~10 MB).
Booted on the A3000 (68060-1107-2326): Total 553644032, available 540499968.

SVR4.0's `anon_resv` needs `anoninfo.ani_max` too, which only `swapadd` raises (by the swap
size): 6 x 64 MB processes failed with 200 MB swap while 515 MB RAM was free. Adding
`maxmem` to `ani_max` (swapfs-like) was tried and is WRONG: 8 x 64 MB then paged out, the
real swap ran out, "memhold was killed due to lack of swap space", and `segu_get` panicked
"swap allocation failure". Reverted: SVR4.0 needs every reservation backed by real swap, so
the 512 MB machine needs swap of at least its anonymous memory (a 2 GB swap disk).

### Step 3: vmemmap only if a hole gets big
If a future layout has a hole of hundreds of MB inside the span, map the hole part of the
page array to one shared read-only tile of "reserved" page_t (60 x 4096 bytes repeat
every 15 pages, so the tile is 60 KB) instead of backing it. A write to a hole entry
then faults, which also catches every site we missed. Not needed for targets 1 and 2.

### Step 4: what we do NOT do
- No real memseg list: it would only buy a smaller array for huge holes, at the cost of
  rewriting ~130 stock sites.
- Not a loadable module: the page array is built in `kvm_init`, before anything loads.

## 5. Test plan (step by step, per the kernel rules)
1. Kernel with step 1 only (region table, span = today's single region): must be bit-for-bit
   equal in behaviour; boot at 192 and 256 MB.
2. Step 2 with the motherboard region only (272 MB, no hole): boot -m, fsck, stress.
3. Step 2 with an artificial hole: pvmk passes 0x08000000-0x0FFFFFFF + 0x10800000-0x17FFFFFF
   (8 MB hole inside known-good RAM). Proves the hole handling on RAM we trust.
   A debug build puts a guard pattern in hole entries and checks it after boot and stress.
4. Only then target 2 with the firmware change.

## 6. Above 1 GB (later, if ever)
The kernel reaches physical memory 1:1 through ITT0/DTT0 (0-0x3FFFFFFF) and uses
0x40000000-0x7FFFFFFF as kernel VA. RAM at 0x40000000+ (Zorro III boards) has two ways in:
- **highmem:** such pages only as user pages; `pagezero`, `pagecopy`, `ppcopy`,
  `gen_strategy` (raw I/O), `prmapout`, RFS reads map them for a moment (kmap). Every
  stock site that does "phys address = kernel address" must be found: big.
- **move the kernel VA** above 0x80000000 and widen the 1:1 window to 2 GB. kvseg,
  segmap, u-area (VA 0x40000000) are link-time/pstart constants; DTT1 already uses
  0x80000000+ for supervisor I/O. Needs its own feasibility study.
Both are far more work than steps 1-2; firmware placement below 0x40000000 is cheaper.
