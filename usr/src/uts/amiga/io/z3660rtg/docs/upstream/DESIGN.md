# DESIGN.md — amix-z3660rtg: the ZZ9000 Amix driver as a Z3660 RTG driver

> **Out-of-repo citations.** Evidence directories are cited by their date-stamped names. They, sibling repositories and archived images are kept outside this repo and are not needed to build or use the driver.

**Status: AGREED 2026-09-15 (decisions in §9). Implemented on branch `z3660-target` (d5671a7 table/accessor/probe with the default rows; 9adfdcd the Z3660 and bench rows); bench step 0 pending (docs/BENCH-STEP0.md).** *(Status line of 2026-09-15.)*

> **Status (2026-09-26).** Initial working version, **metal-proven end to end**: the candidate-2 kernel (driver 7e5c0e8) + the cross-built `Xzz9000` brought the OpenLook desktop up on the Z3660 HDMI from a real A4000 (metal session 2, rows 0–7, `docs/DEMO-RTG.md`; status of record 2026-09-19). Performance is poor; it is the next lane. Whether ZZ9000 and Z3660 stay one driver is still open.

Author: the amix-z3660rtg driver side. Citations are `file:line` against upstream 75e2449 (this
repo), the Z3660 firmware tree (`Z3660/z3660-firmware/Z-TURN/vitis_ide/`, abbreviated
`FW/`), the Z3660 AmigaOS drivers (`Z3660/z3660-drivers/`, abbreviated `DRV/`), the
amiberry fork (github.com/jusii/amiberry, branch `amix-v830`, not yet public), the sibling drivers and the grimoire. Confidence: ✅ read in
source / measured, 🟡 secondary document, ❓ open (listed in §5 and §10).

The document answers: (1) what in the upstream driver is *identity/window* knowledge versus
*register protocol*; (2) what the Z3660 RTG actually looks like from the guest; (3) the single
switch that separates the targets and exactly what it changes; (4) what only the Z3660
firmware side's `FIRMWARE-RTG-INTERFACE.md` (or metal) can still close; (5) bench and compiler
constraints; (6) the project decisions asked for.

---

## 0. Summary — including one correction to the brief

- **The brief's premise "register interface is ZZ9000-compatible" is true at the protocol
  level and false at the byte level.** The Z3660 firmware keeps the ZZ9000's register *names,
  order and semantics* (MODE = `id | color<<8 | scale<<12`, the blitter registers, PAN as a
  byte offset, the same colour modes) but exposes them as a **32-bit-word register file
  starting at `0x100`** (`FW/Z3660/src/rtg/zzregs.h:28-78`; `DRV/rtg/gfx.c:211-212,485`
  `registers[(reg)>>2]`), not the ZZ9000's 16-bit registers at `0x00..0xc0` that upstream
  (`zz9000.h:19-50`) and amiberry (`src/zz9000.cpp:44-89`) speak. There is no `HW_VERSION`,
  `FW_VERSION` moved to `0x1A0` and reads `0x0103`, `PAN_HI/LO` became one register at
  `0x120`, the capture switch is a stub, the framebuffer starts at **`+0x200000`**, and mode
  id 17 is **1280x800**, not 1920x800 (`zz_video_modes.h:21`). Pointed at `0x10000000`,
  upstream's driver would read `0xffff/0xffff` and declare the board absent.
- Everything else in the brief holds: with `autoconfig_rtg NO` (every Amix preset) the
  RTG/ethernet/piscsi combo window is hard-mapped at guest `0x10000000` with no AutoConfig
  entry (`FW/Z3660_emu/src/cpu_emulator.cpp:221-236`), inside the kernel's identity-mapped
  low 1 GB, so it is reached the way the sibling drivers reach it: AGA-gated fixed-base probe,
  signature read, plain dereference, no `sptalloc`.
- **Proposed switch: one board-table row per target, rows compiled in by a single
  `-DZZ9K_TARGET_Z3660`.** A row carries identity (autocon code or fixed base + gate),
  window (register block, framebuffer offset/size), the register map + access width, the
  mode table, presence test and capture semantics. All upstream sites that today read a
  `ZZ9K_*` macro read `bp->bt->…` instead; the default build's rows reproduce upstream's
  behaviour exactly (bench step 0 proves it). The delta is larger than an identity swap
  because the register file differs, but it is still data plus one probe function plus a
  ~30-line register accessor — no `#ifdef` inside any function.
- Unchanged: `/dev/zz9000`, char major 49, the ioctl ABI, `zz9k_test`'s flow, the
  `Xzz9000` server (it mmaps `board_size` and takes `base + fb_offset`; it never assumes
  `0x10000` or 4 MB — `x11r6.3-amix/…/rtg/zz9000/zz9000hw.c:114-136`).
- Two kernels, two compilers: bench = golden 68030 build, GNU GCC 1.40.5, 2 KiB pages;
  metal = 68LC060 Model-B kernel from `amix-040-060-port`, cross gcc 2.7.2.3, **4 KiB
  pages**. The one trap is `phystopfn()` in `zz9000mmap` (§7).

---

## 1. What we start from (bench-proven)

Evidence `2026-09-15-zz9000-rig/` (amix-kerntools 09d4b6f + 00c316c; `11-report.md`,
`31-report-resume.md`). ✅

| Fact | Evidence |
|---|---|
| Upstream 75e2449 compiles with the golden's **GNU GCC 1.40.5** once the Makefile's gcc-2 `-fno-builtin` is stripped; the C needs no change (zero diagnostics for `zz9000.c`, `zz9k_test.c`, `colorls.c`) | `04-build-zz9000.log`, `05-/06-gcc1405-*`, kerntools `chardevs.conf` `cflags_strip=-fno-builtin` |
| Kernel 09684 boots in the piscsi rig; `autocon match pc 0x6D6E0003 base 0x200000 size 0x400000` / `Zorro II board … hw 0x0 fw 0x207` | `19-rig-boot-zz9000-kernel.png` |
| `zz9k_test /dev/zz9000 16 1920x800`: `fb 0x10000+0x3d0000`, `MODE readback 0x0111`, bars on the emulated card | `22-zz9k_test-1920x800.txt`, `25-rtg-1920x800-16-bars.png` (pattern checked by eye here) |
| The driver's own framebuffer console at 1920x800, sunfont, scroll + cursor | `27-rtg-console-b.png` |
| `bootinfo.autocon[]` on the rig: `[0]` 0xc7 Z2 0x6d6e/3 at 0x200000 4 MB; `[1]` A2065; `[2]` 0x83 Z3 **0x144b/1 at 0x40000000 128 MB** (amiberry's piscsi model) | `09-amix-bootinfo-autocon.txt` |
| amiberry bug, not a driver bug: `zz_set_pan` takes the pitch from blitter X2 on every PAN_LO write (`src/zz9000.cpp:965-975,1364-1370`); the driver's SWITCH_RTG writes PAN=0 with X2==0 → black. No fix in the fork's log yet | `24-…pitch0.png`, `zzpanfix.c` |
| Kernel wiring via kerntools `chardevs.conf` (sha-pinned subtree push, on-box `ed` of `init_tbl[]`/`cdevsw[49]`/driver Makefile); `c1.c` console hook OFF | `chardevs.work.conf:33` |

The chardevs record points at the isoriano clone under `2026-09-14-isoriano-repos/`; the
bench of *this* fork needs it repointed to `amix-z3660rtg/overlay/…` with a new sha pin
(cross-repo note, §6).

---

## 2. Inventory: identity/window knowledge vs register protocol in upstream

### 2.1 `zz9000.h`

| Constant | Value | Class | Z3660 verdict |
|---|---|---|---|
| `ZZ9K_MNT_MANUFACTURER`, `ZZ9K_PRODUCT_Z2/_Z3`, `ZZ9K_PRODUCT`, `ZZ9K_PRODUCT_PC_Z2/_Z3` (`:11-16`) | 0x6d6e, 3, 4, packed `(mfg<<16)\|prod` | **identity** | Z3660 mfg is 0x144B; the RTG has no AutoConfig identity of its own under `autoconfig_rtg NO` |
| `ZZ9K_MAX_AUTOCON` (`:17`) | 16 | kernel | neutral |
| `ZZ9K_REG_*` (`:19-50`) | 16-bit at 0x00…0x5e, 0xc0, 0x1000…0x1006 | **protocol, ZZ9000 byte layout** | **differs**: 32-bit at 0x100… (§3.3) |
| `ZZ9K_REG_SIZE` (`:52`) | 0x2000 | window | still right: Z3660 registers end below 0x900, `0x1000-0x1FFF` is a firmware test area, piscsi starts at 0x2000 — the bound keeps GETREG/SETREG off the SCSI mailbox |
| `ZZ9K_FB_OFFSET` (`:53`) | 0x10000 | **window** | **0x200000** on the Z3660 |
| `ZZ9K_Z2_RESERVED_TAIL` (`:54`) | 0x20000 | **window** | none; instead an upper bound at +0x3000000 (§3.2) |
| `ZZ9K_FW_VER_MIN` (`:55`) | 0x010d | **identity** | Z3660 FW_VERSION = 0x0103 in another numbering |
| `ZZ9K_COLOR_*` (`:57-60`) | 0-3 | protocol | same (15-bit is remapped to 565 by the firmware) |
| `ZZ9K_MODE_*` (`:62-81`) | ids 0-17 | protocol, **table differs from 12 up** | §3.4 |
| `ZZ9K_SWITCH_*`, `ZZ9K_CAPTURE_PAN[_VGA]` (`:83-87`) | | ZZ9000 capture concept | no capture path on the Z3660 (§3.5) |
| `ZZ9K_IOC*`, `ZZ9K_CONSOLE_*`, `struct zz9k_*` (`:89-197`) | | **ABI** | bit-identical, both targets |

### 2.2 `zz9000.c`

| Site | Class | Assumes |
|---|---|---|
| header comment (`:1-8`) "AMIX has no Zorro III support" | doc | imprecise: `autocon()` returns Zorro III bases; the wall is the identity map above `0x3FFFFFFF` (grimoire `zorro-autoconfig.md:52-64`) |
| `struct zz9k_board` (`:87-98`) | window | no per-board `fb_offset`, register map, mode table |
| `ZZ9K_REG()` macro (`:24-25`) and its ~40 uses | **protocol, 16-bit** | every register access is a 16-bit volatile at a ZZ9000 offset |
| `zz9k_present()` (`:260-274`) | presence rule | HW@0x00 and FW@0xc0 not both 0/0xffff — on the Z3660 both offsets are unmapped → "absent" |
| `zz9k_attach()` (`:277-309`) | window + identity | `fb_size = size - FB_OFFSET - Z2_TAIL`; "Zorro II board"; `FW_VER_MIN` |
| `zz9k_probe()` (`:331-362`) | **identity** | `autocon(PC_Z2)` → attach; `autocon(PC_Z3)` → reject; runs from `zz9000init()` and lazily from rw/mmap/ioctl |
| `zz9k_mode_id()` (`:160-179`) | protocol, **table** | width/height → ZZ9000 id; 1920x800 → 17 |
| `zz9k_fill_fbinfo()` (`:198-258`) | ABI | `fb_offset = ZZ9K_FB_OFFSET` (`:203`) |
| `zz9k_write32` (`:364-372`), `zz9k_set_mode_hw` (`:484-507`), `zz9k_set_switch_hw` (`:509-521`) | protocol | HI/LO split writes; zeroes X1/Y1/X2/Y2/COLORMODE/SRC/DST/SPLIT_POS, PAN=0, then MODE; switch = CX_CAPTURE + PAN |
| `zz9k_blit_fill/copy` (`:374-416`) | protocol, **dead code** | never called |
| CPU fill/copy (`:418-482`), FILL ioctl (`:1348-1363`) | window | `base + ZZ9K_FB_OFFSET` at `:431,:454,:1360` |
| console renderer (`:523-1140`) | window + geometry | `ZZ9K_FB_OFFSET` at `:645,:658,:696,:736`; fixed 1920x800 / mode 17 / RGB565 (`:147-150`) |
| `zz9000rw()` (`:1179-1210`) | window | `uiomove` from the kernel VA `bp->base` over `[0, board_size)` — needs the identity map |
| `zz9000mmap()` (`:1230-1247`) | window + **page geometry** | `phystopfn(base + offset)` over `[0, board_size)` |
| `ZZ9KIOC_GETINFO` (`:1297-1312`) | identity | `product = 3`, `zorro = 2`, `fb_offset = ZZ9K_FB_OFFSET` |
| GETREG/SETREG (`:1404-1420`) | protocol | raw 16-bit at `offset < 0x2000` |

### 2.3 `tools/zz9k_test.c`

`print_z2_expected()` (`:21-33`, macros, cosmetic); `print_autocon()` + the gate `:312-321` that
**exits unless an autocon entry 0x6d6e/3 exists** — blocks every test on a hard-mapped Z3660;
`print_regs()` (`:140-168`) reads HW/FW/MODE/CONFIG/VCAP by raw ZZ9000 offsets; `mmap(0,
info.board_size)` (`:516`) — maps whatever the driver reports; the messages at `:315,:326-327`.

### 2.4 Target-neutral

`integration/*.txt` (init_tbl, cdevsw 49, `displayedscreen` hook), `mkdev.sh`, `init/S99zz9000`,
`tools/zz9k_console`, `zz9k_colorls_env`, `colorls.c`, `install-source.sh`; the Makefile apart
from `-fno-builtin` (gcc-2 only) and the optional target define.

### 2.5 Pure protocol shared by both targets

MODE/colour/scale encoding, PAN as a framebuffer byte offset, the "X2 doubles as pitch, 0 =
native width" convention (`FW/Z3660/src/video.c:809-812`; amiberry gets this wrong), `zz9k_bpp`,
`fbinfo` masks, the console's ANSI state machine, every ioctl's semantics and struct layout,
the X server contract (`PROBE → SETMODE → GETINFO/GETFBINFO → mmap(board_size) → FILL →
SETSWITCH RTG`; close `SETSWITCH CAPTURE` + `munmap`).

---

## 3. The Z3660 RTG as the guest sees it

From the firmware and AmigaOS-driver sources, confirmed by the Z3660 firmware side's answer sheet
(`docs/FIRMWARE-RTG-INTERFACE.md`, merged 2026-09-15; verbatim answers in
`docs/FIRMWARE-ANSWERS-2026-09-15.md`) — nothing in this section is contradicted by it. The demo machine runs the **real 68LC060**
(`bootmode CPU`), so the bus path that matters is the FPGA→ARM handler `rtg_thread()`
(`FW/Z3660/src/rtg/rtg.c:552-720`), not the on-ARM UAE addrbanks used by the emulated-CPU
modes; both dispatch into the same `read_rtg_register()`/`write_rtg_register()`.

### 3.1 Identity and placement

| | `autoconfig_rtg NO` (shipped default, every Amix preset) | `autoconfig_rtg YES` |
|---|---|---|
| Base | **`0x10000000`**, hard-mapped by `reset_autoconfig()`, `configured_z3 \|= 2` so it never enters the guest's AutoConfig walk (`cpu_emulator.cpp:221-236`; `amix_ram.h:52-75`) ✅ | a Zorro III board: `er_type 0x83/0x93` (emulated path, `cpu_emulator.cpp:325-336`) / `0x8B/0x9B` (FPGA path, `z3660.v:973`), both 128 MB; **product `0x01`** (`cpu_emulator.cpp:338-345` "1 for the RTG PIC", `z3660.v:981`); manufacturer **0x144B** (`cpu_emulator.cpp:263`, `z3660.v:993-996`; `docs/real-cpu-autoconfig-and-ext-rom.md:96-102`) ✅ — i.e. `0x144B0001`, the code the siblings and amiberry use; lands at `0x40000000`, or `0x50000000` when the 256 MB RAM board autoconfigures first (🟡 `docs/amix-firmware-facts.md` §5) |
| `bootinfo.autocon[]` entry | none ✅ | Amix 2.1's table is documented to miss it anyway (grimoire `z3660-scsi-driver.md:194-201`) 🟡 |
| Kernel reachability | inside the identity-mapped low 1 GB → plain dereference, `phystokv(p) == p` (`amix-z3660scsi/src/z3660.c:640-651,711-745`) ✅ | above `VSECT1 = 0x40000000` → `sptalloc()` per page; the pool is 4 MB (`z3660.c:664-669`) |
| 060 data cache | `DTT0 = 0x003fc060` cache-inhibits the whole low 1 GB (`z3660.c:688-709`) ✅ | unverified: a kernel `sptalloc` mapping's cache class was never measured (the port's r13 note covers user device mmaps, which are NCS); unsupported by the siblings with a live cache |
| Sibling-driver product code | `Z3660_PROD 0x144B0001` = "Z3 RTG + piscsi combo" (`z3660.c:77`, `z3660eth.h:15`), consistent with the ROM bytes above and with amiberry's model (`src/z3660_scsi.cpp:49-51`) ✅ | |

**v1 supports the NO configuration only**; the YES row is recognised and refused with advice,
the way upstream refuses product 4.

### 3.2 The 128 MB window at `0x10000000` (offsets relative to base)

`init_rtg_bank(0x100)`: `RANGE_MAP(0x1000,0x1020, rtg_regs_bank)` + `RANGE_MAP(0x1020,0x1800,
rtg_bank)` in 64 KB banks (`FW/Z3660_emu/src/uae/uae_emulator.cpp:1019-1024`) = a **2 MB
ARM-handled region** then **126 MB of RTG RAM**; the real-CPU handler masks `gpio & 0x1FFFFF`
(`rtg.c:570-571,644`) — the same 2 MB. ✅

| Offset | What | Owner |
|---|---|---|
| `0x000000-0x000FFF` | RTG register file (registers at `0x100-0x89C`, the last being the ADF-name text block `0x800..0x895`, `rtg.c:787-795`; `0x000-0x0FF` and `0x8A0-0xFFF` unmapped, reads `0xFFFFFFFF`, writes ignored `rtg.c:2417-2420`) | this driver |
| `0x001000-0x001FFF` | firmware **"test area"**: raw memory + an **unconditional** `printf` on the ARM UART per access (not gated by `debug_console`), checked before the register range on both paths (`rtg.c:573-577,646-673`) — where upstream's `CX_*` writes would land; a userland loop over the page blocks the RTG thread on UART writes | nobody; **excluded from the row's device space** (§4.4) |
| `0x002000-0x005FFF` | piscsi mailbox (`0x2400-0x25FF` floppy) (`rtg.c:584-590,687-694`) | amix-z3660scsi |
| `0x006000-0x025FFF` | SCSI boot ROM (`memorymap.h:37-38`) | firmware |
| `0x080000-0x08FFFF` | piscsi bounce buffer (`SCSI_NO_DMA_ADDRESS`, `memorymap.h:36`; `z3660.c:80-81`) | amix-z3660scsi |
| **`0x200000`** | **framebuffer start** (`FRAMEBUFFER_ADDRESS = RTG_BASE+0x200000`, `memorymap.h:18`; `DRV/rtg/gfx.c:124,504` `Z3660_MEMBASE_ADDR`) ✅ | this driver |
| `0x200000-0x2FFFFFF` | RTG memory the AmigaOS driver claims: `MemorySize = 0x3000000 - 0x200000` = 46 MB (`gfx.c:511`) ✅ | this driver |
| `0x3200000` / `0x3210000` | GFXData scratch / templates for the DMA-op path (`memorymap.h:21`; `gfx.c:125-126`) | AmigaOS driver's DMA ops — unused by us |
| `0x4200000…` | soft3D buffers (`memorymap.h:24-25`) | |
| `0x7CE0000`, `0x7D00000` | audio TX/RX buffers | |
| `0x7E00000-0x7EFFFFF` | ethernet BD lists, RX backlog `0x7ED0000`, TX frame `0x7EE0000`, RX frame `0x7EF0000` — spelled as ARM-side absolute addresses in `memorymap.h:30-34`, bound board-relative by the net driver (`z3660eth.h:36-45`); the ARM MMU reserves RTG-relative MB `0x60-0x80` (top 32 MB) for overlay/audio/mpeg/eth (`cpu_emulator.cpp:432-459`) | amix-z3660net |
| `0x7F00000` | USB data | |

The `memorymap.h:43-50` comments (`//#define MNT_REG_BASE 0`, `//#define MNT_FB_BASE 0x10000`)
show the ZZ9000 layout was deliberately replaced.

### 3.3 Register compatibility table (registers upstream touches, plus the notable absentees)

ZZ9000 column = upstream `zz9000.h` and amiberry `src/zz9000.cpp:44-89` (16-bit, board+off).
Z3660 column = `FW/Z3660/src/rtg/zzregs.h` and `rtg.c` (32-bit, board+off, **aligned 32-bit
access only**: on the real-CPU path `rtg_thread()` re-reads the word at the raw address after
the FPGA lands the bus write in DDR (`rtg.c:570-580`), ignores the size bits on the register
path (they are honoured only for piscsi, `:596-610`), and `write_rtg_register()` aligns with
`zaddr & 0x1FFFFC` (`:1039-1042`) — so a 16-bit write at `0x100` is dispatched as
`V<<16 | stale(0x102)`, one at `0x102` as `V<<16 | stale(0x104)` to the `0x100` handler: a
HI/LO pair never composes. Reads: a 16-bit read gets one lane of the 32-bit value
(`:676-683`) — `FW_VERSION` read as a word at `0x1A0` is `0x0000`, at `0x1A2` `0x0103`.
**MODE, COLORMODE, X1/Y1/X2/Y2, ROW_PITCH and VBLANK_STATUS are write-only** on the Z3660
(`read_rtg_register` has no case for them; VBLANK's is commented out `:840-841`) → reads
 return `0xFFFFFFFF`, so `zz9k_test`'s "MODE readback" line cannot reproduce there. A MODE
write whose (mode, colour, scale) triple equals the cached one is a no-op (`:1120-1123`).
Handler line numbers are in `rtg.c` unless noted.

| Logical register | ZZ9000 | Z3660 | Verdict |
|---|---|---|---|
| HW_VERSION | `0x00` r16 (amiberry: 0) | **none** — `0x000-0x0FF` unmapped, read → `0xFFFFFFFF` (`:1023-1027`) | absent; presence test must change |
| MODE | `0x02` w16 `id\|color<<8\|scale<<12` | `0x100` w32, same formula (`:1111-1132`) | same semantics |
| CONFIG | `0x04` | `0x104` = eth/audio/usb interrupt enable/ack (`:1087-1110`) | never written by us ✓ (the net driver owns it) |
| SPRITE_X/Y | `0x06/0x08` | `0x108/0x10C` | unused |
| PAN | `0x0a` HI + `0x0c` LO | **`0x120` single w32**; firmware adds `(X1<<cm) + Y1*(X2<<cm)` and snapshots `pan_width = X2` (`:1055-1066`) | merged; keep X1=Y1=0 (upstream does) |
| VCAP_MODE | `0x0e` | none | unused by the driver; `zz9k_test` `print_regs` reads it (cosmetic) |
| BLITTER X1/Y1/X2/Y2 | `0x10/12/14/16` | `0x110/114/118/11C` | same order |
| ROW_PITCH | `0x18` | `0x124` | |
| X3/Y3 | `0x1a/0x1c` | `0x128/0x12C` | |
| RGB | `0x1e` HI + `0x20` LO | `0x130` w32, byte-swapped by the firmware (`:1214 rect_rgb = swap32(zdata)`) | merged; only matters if the dead blitter paths are revived |
| OP_FILLRECT / OP_COPYRECT | `0x22/0x24` | `0x134/0x138`, implemented (`:1239-1284`) | dead code in the driver |
| SRC / DST | `0x28/2a`, `0x2c/2e` | `0x140`, `0x144` w32 | merged |
| COLORMODE | `0x30` | `0x148` (15-bit → 565 remap `:1083-1085`) | |
| SRC_PITCH | `0x32` | `0x14C` | |
| VBLANK_STATUS | `0x4c` | `0x17C` | unused |
| SPLIT_POS | `0x5e` | `0x188` (`SET_SPLIT_POS`) | written 0 in `set_mode_hw` |
| FW_VERSION | `0xc0` r16; amiberry `0x0207`; upstream floor `0x010d` | `0x1A0` r32 = `(REVISION_MAJOR<<8)\|REVISION_MINOR` = **0x0103** (`:805-807`, `version.h:1-2`); `FW_BETA 0x280` = 21, `FW_ALFA 0x284` | different numbering; the row's floor is 0x0103 |
| CX_DATA_HI/LO, CX_OP | `0x1000-0x1004` (amiberry: palette latches) | firmware **test area** (§3.2); the palette equivalents are `REG_ZZ_OP_DATA 0x300` / `REG_ZZ_OP 0x304` (`:1887-1901`) | never written by us; 8-bit CLUT is unsupported on both targets (the driver sets no palette) |
| CX_CAPTURE | `0x1006` (capture ↔ RTG) | none; `REG_ZZ_OP_CAPTUREMODE 0x30C` is a **stub** (`:1905-1908`, `zz_set_monswitch` commented out); the AmigaOS driver still writes it (`DRV/rtg/gfx.c:1643-1669`) | the switch is a no-op on the Z3660 |
| BLITTER_DMA_OP / GFXData path | none in upstream | `0x180` + scratch at `+0x3200000` — how the AmigaOS driver pans/blits on Zorro III (`gfx.c:1683-1698`) | not needed: the direct `REG_ZZ_PAN` handler exists |
| Register block bound | `0x2000` | registers end at `0x89C`; `0x2000+` is piscsi | keep `0x2000` |
| Read of an unmapped register | (amiberry: 0) | `0xFFFFFFFF` (`:1023-1026`) | |
| Presence test precedent | upstream: HW/FW not both 0/0xffff | the AmigaOS driver accepts the board iff `FW_VERSION >> 8 == 1` (`DRV/rtg/gfx.c:488-495`) | the Z3660 row's `zz9k_present()` follows the AmigaOS driver |

### 3.4 Modes and colour

`REG_ZZ_MODE` accepts `id < ZZVMODE_NUM` (`:1114-1115`). Ids (`zz_video_modes.h:3-27`) vs
upstream (`zz9000.h:62-81`) and amiberry (`zz9000.cpp:387-406`):

| id | ZZ9000 (upstream/amiberry) | Z3660 |
|---|---|---|
| 0-11 | 1280x720, 800x600, 640x480, 1024x768, 1280x1024, 1920x1080, 720x576p50, 1920x1080p50, 720x480, 640x512, 1600x1200, 2560x1440p30 | **same** |
| 12-15 | 1920x1080_NS, 1280x720_NS, 800x600_NS, 640x480_NS | 720x576/720x480 NS PAL/NTSC variants — **differ** (never produced by `zz9k_mode_id`) |
| 16 | 640x400 | same |
| 17 | **1920x800** (the driver's console mode, the tested primary) | **1280x800** |
| 18-22 | — | 1920x1200, 1600x900, 1680x1050, 1366x768, CUSTOM |

Colour modes 0-3 identical; scale bits: bit 0 halves horizontally, bit 1 vertically
(`video.c:1227-1259`). **1920x800 does not exist on the Z3660**: the console geometry and
`zz9k_mode_id()` become row data; the Z3660 console default is proposed as 1920x1080 (id 5,
common to both) — decision 9.4.

### 3.5 Capture / display switch

The Z3660 has no native-video capture input and no path to a native Amiga screen: the firmware
owns the single HDMI output, drives its own boot splash/console into the same framebuffer at
boot (`video.c:245-270,698`), and a guest `MODE` write reprograms that output (`rtg.c:1111-1132`);
`OP_CAPTUREMODE` is a stub, `zz_set_monswitch` is never called, `REG_ZZ_MONITOR_SWITCH 0x318` is a
preset field (Q6, closed). So `SETSWITCH CAPTURE` / "noconsole" cannot restore anything — the
row's `NOCAPTURE` flag makes them no-ops by construction. Which physical panel the demo operator
watches remains a metal observation (A2).

### 3.6 Pitch convention

`video.c:809-812`: VDMA stride = `hsize * (Mm2SStreamWidth>>3)` unless `framebuffer_pan_width`
(= X2 at the last PAN write) is non-zero *and* differs from `hsize/hdiv` (the width in VDMA
words, `hdiv` carrying the colour divisor, `:1246-1255`); a pixel-width X2 lands on the same
stride after `/hdiv` (`:818`). Upstream's sequence (X2=0, PAN=0) is therefore correct on metal;
amiberry needs its fix (§1).

---

## 4. The single switch

### 4.1 Options

| | A. `#ifdef Z3660` at every site | B. probe-time table, always compiled | **C. table + one compile-time row selector** |
|---|---|---|---|
| Seams | ~55 (`ZZ9K_REG` uses, `FB_OFFSET` sites, mode table, presence, probe, tools) | 1 table + 1 probe loop + 1 accessor | same as B |
| Fixed-base probe on an AGA machine without a Z3660 | never compiled | **fires** at `0x10000000` with no AutoConfig evidence — a bus-error risk for isoriano's own users | never compiled unless built for the Z3660 |
| One kernel for both cards | no | yes | no — the targets already need different kernels (030 golden vs 060 Model-B) |
| Upstream readability | poor | good, but the safety argument is runtime | good: rows are data; the selector is one Makefile line |

**Recommendation: C.** If a Z3660 signature that is provably safe to read on any AGA machine
ever exists, the selector can become a default-on row without touching the sites.

### 4.2 The delta, exhaustively

`zz9000.h` — additions only; the ABI block is untouched:
```c
/* logical register ids -- the row maps them to offsets; ZZ9K_NOREG = absent on this board */
#define ZZ9K_R_HW_VERSION 0
#define ZZ9K_R_MODE       1
#define ZZ9K_R_PAN        2   /* 32-bit value; 16-bit rows write HI at off, LO at off+2 */
#define ZZ9K_R_X1 3 ... ZZ9K_R_Y2, ZZ9K_R_COLORMODE, ZZ9K_R_SRC, ZZ9K_R_DST, ZZ9K_R_SPLIT_POS,
        ZZ9K_R_FW_VERSION, ZZ9K_R_CAPTURE, ZZ9K_R_ROW_PITCH, ZZ9K_R_X3, ZZ9K_R_Y3, ZZ9K_R_RGB,
        ZZ9K_R_FILLRECT, ZZ9K_R_COPYRECT
#define ZZ9K_NREG   ...
#define ZZ9K_NOREG  0xffff

struct zz9k_modetab { unsigned short width, height, id; };

struct zz9k_boardtab {
    char *name;                   /* "ZZ9000 Zorro II", "Z3660 RTG (fixed)", ... */
    unsigned long autocon_pc;     /* (mfg<<16)|product, 0 = not AutoConfig */
    unsigned long fixed_base;     /* when autocon_pc == 0 */
    unsigned long fixed_size;     /* window size for a fixed row */
    unsigned long fb_offset;      /* device-space offset reported in GETINFO/GETFBINFO */
    unsigned long fb_phys;        /* physical offset of the framebuffer inside the window (== fb_offset on the ZZ9000) */
    unsigned long fb_max;         /* 0 = to end-of-window minus reserved_tail */
    unsigned long reserved_tail;  /* ZZ9000 Z2: 0x20000 */
    unsigned long reg_size;       /* GETREG/SETREG bound: ZZ9000 0x2000, Z3660 0x1000 (keeps the test area out) */
    unsigned short regw;          /* register access width: 2 or 4 */
    unsigned short *regoff;       /* [ZZ9K_NREG] offsets or ZZ9K_NOREG */
    struct zz9k_modetab *modes;   /* width/height -> id, terminated by {0,0,0} */
    unsigned short con_w, con_h;  /* console geometry (must be in `modes`) */
    unsigned short fw_min;        /* 0 = no floor */
    unsigned short zorro, product;/* GETINFO cosmetics */
    unsigned short flags;         /* ZZ9K_BT_REJECT | ZZ9K_BT_AGAGATE | ZZ9K_BT_NOCAPTURE */
};
```
`struct zz9k_board` gains `struct zz9k_boardtab *bt;` and a cached `char *fb` (kernel VA of the
framebuffer). `zz9000.c`:
- `zz9k_rd(bp, r)` / `zz9k_wr(bp, r, v)` / `zz9k_wr32(bp, r, v)`: ~30 lines; a `ZZ9K_NOREG` read
  returns 0xffff, a write is skipped. Every `ZZ9K_REG(bp->base, ZZ9K_REG_X)` site becomes
  `zz9k_wr(bp, ZZ9K_R_X, …)`; `zz9k_write32(PAN_HI/SRC_HI/DST_HI/RGB_HI, v)` becomes
  `zz9k_wr32(bp, ZZ9K_R_PAN/…, v)`. GETREG/SETREG keep raw offsets, bounded by the row's `reg_size` and using its width.
- `zz9k_probe()` loops over the rows: autocon rows as today (REJECT rows keep upstream's
  messages); fixed rows: `AGAGATE` → `VPOSR` `0xDFF004`, `((v>>8)&0x7f) >= 0x22`
  (`z3660.c:720-726`), then `zz9k_present()` on the row (see below), then the siblings'
  direct-map rule `base + window <= 0x40000000` else refuse.
- `zz9k_present()` per row: `FW_VERSION` (and `HW_VERSION` when the row has one) not 0 and not
  all-ones; for the Z3660 additionally `(fw >> 16) == 0`.
- `zz9k_attach()`: `fb_size = fb_max ? fb_max : size - fb_offset - reserved_tail`; `bp->fb = base + fb_phys`.
- Framebuffer address translation for the Z3660's compacted device space (§4.4): one helper
  `zz9k_devphys(bp, off)` used by `zz9000rw`, `zz9000mmap`; kernel-side users take `bp->fb`.
- `zz9k_mode_id()` walks `bp->bt->modes`; the console reads `con_w/con_h` into two statics at
  `zz9k_console_start()` instead of `ZZ9K_CON_WIDTH/HEIGHT`; `ZZ9K_CON_MODE` is looked up.
- `zz9k_set_switch_hw()`: `NOCAPTURE` → return.
- GETINFO reports the row's `zorro`/`product`/`fb_offset`.

`Makefile`: honours two make variables (default empty) folded into `ZCDEFS`/`CFLAGS`:
**`ZZ9K_TARGET`** for the target (`-DZZ9K_TARGET_Z3660`) and **`ZZ9K_BENCH`** for a bench aid
(`-DZZ9K_BENCH_Z3` or `-DZZ9K_BENCH_Z3660`) — two variables because the build hub passes one
token per variable (its `make=` grammar is comma-separated `VAR=token` pairs, no spaces). **Build-hub contract (kerntools c011498):** the chardevs record
carries a `make=` pass-through, e.g. `make=ZZ9K_TARGET=-DZZ9K_TARGET_Z3660`, which lands on the
generated driver rule as `cd zz9000; $(MAKE) ZZ9K_TARGET=…` — so the driver Makefile alone
defines what the variable means, a record without it reproduces upstream byte for byte, and
the target build is a second record (or a `CHARDEVS_CONF=` work conf). No cflags column. `tools/zz9k_test.c`: the autocon gate becomes advisory
(always `PROBE`), `print_z2_expected` prints what `GETINFO` says, `print_regs` uses GETREG at
the row's offsets via a `ZZ9KIOC_GETINFO` field or simply prints MODE/FW through the driver.

Rows compiled in (**corrected on metal, 2026-09-18** — see the box below):
- default: `ZZ9000 Zorro II` (accept, `0x6d6e0003`, fb 0x10000, tail 0x20000, regw 2, ZZ9000
  regoff, ZZ9000 modes, con 1920x800, fw_min 0x010d, zorro 2 product 3) and `ZZ9000 Zorro III`
  (REJECT, `0x6d6e0004`) — upstream's behaviour and messages;
- `-DZZ9K_TARGET_Z3660` adds **`Z3660 RTG`** (the board as AutoConfig lists it, `0x144B0001`: it
  **attaches** when the entry's base is inside the identity map, and is refused with the advice
  "set autoconfig_rtg NO" only when it is not) and **`Z3660 RTG (fixed)`**: base 0x10000000,
  `AGAGATE|NOCAPTURE`, regw 4, Z3660 regoff, Z3660 modes, con 1920x1080 (id 5), reg_size 0x1000,
  fb_offset 0x1000, fb_phys 0x200000, fb_max 0x2E00000 (the AmigaOS driver's 46 MB, `gfx.c:511`),
  fw_min 0x0103 with the test `(fw >> 8) == 1`, zorro 3, product 1.

> **Metal correction (2026-09-18, first boot of the RTG card).** The driver printed
> `zz9000: Z3660 combo board at 0x10000000 size 0x8000000 unsupported` and refused a board it
> can use. The assumption behind the REJECT row — *a board in the AutoConfig table must be the
> `autoconfig_rtg YES` case at `0x40000000`* — is false on real hardware: with
> `autoconfig_rtg NO` the hard-mapped combo **still enters the board list** (it advertises
> itself for SCSI boot, which is how the card boots), so the table lists it at `0x10000000`
> with the 128 MB window. Neither the firmware sources nor the emulator showed this.
> **The discriminator is the base address, not the presence of an entry.** Since 62da6c5+ the
> autocon row carries the full RTG window and attaches when its base is inside the identity
> map; the generic identity-map check refuses it with the advice when it is not, and — the
> second half of the fix — a refused or unanswering row now lets the **next** row try instead
> of ending the probe, which is the sibling drivers' multi-method shape and makes
> `-DZZ9K_BENCH_Z3660` unnecessary (the define is accepted and ignored).

### 4.3 What stays identical

`/dev/zz9000` c 49 0, every ioctl number and struct, `Xzz9000`, `zz9k_test`'s command set,
scripts, the console renderer's behaviour, the safety model (no console takeover from
`init_tbl[]`; late `S99zz9000` only).

### 4.4 Device space on the Z3660: compacted, not raw (decision 9.3)

`Xzz9000` and `zz9k_test` `mmap()` the whole `[0, board_size)` from offset 0 and only ever
touch `base + fb_offset` (`zz9000hw.c:114,136`). If the Z3660 row exposed the *raw* window
(`fb_offset = 0x200000`), the mapping would hand userland RW access to the piscsi mailbox
(`+0x2000`), the boot ROM and the SCSI bounce buffer (`+0x80000`), and the node is `0666`
(`mkdev.sh`). Proposal (accepted, refined twice): after Q8 the device space was the register page followed
by the framebuffer (`fb_offset 0x1000`, bench-proven in step 2); after the 68040/68060 port's
finding that the same page carries the **ethernet driver's doorbells** (`+0x104..+0x1AB`,
`amix-z3660net/src/z3660eth.h`), the Z3660 row's device space is **the framebuffer alone**
(6137966): `fb_offset = 0`, `fb_phys = 0x200000`, `board_size = fb_size = 0x2E00000`;
`zz9k_devphys()` maps every device offset into the framebuffer. Userland never maps the
register page, the firmware's printf-per-access test area, the piscsi mailbox, ROM or bounce;
registers are reached through GETREG/SETREG only, bounded by `reg_size = 0x1000`. `Xzz9000`
and `zz9k_test` are unaffected (they take `fb_offset` from the driver). On the ZZ9000 rows
`fb_phys == fb_offset` and the helper is the identity. Alternative: raw window + a `0600`
node; simpler, and leaves the hazard.

### 4.5 Not in v1

`autoconfig_rtg YES` (window above the identity map, `sptalloc` pool too small for a
framebuffer); a real ZZ9000 product 4 (same argument); the hardware blitter (dead in upstream
too); the DMA-op/GFXData path (AmigaOS-only convenience).

---

## 5. Open questions for `FIRMWARE-RTG-INTERFACE.md` (or the metal)

| # | Question | Why it matters | Preliminary answer |
|---|---|---|---|
| Q1 | ~~Is the hard-map at `0x10000000` unconditional under `autoconfig_rtg NO`?~~ **closed**: gated by exactly `autoconfig_rtg` (`main.c:1694`), both boot modes (`main.c:1436,1440,1662-1665`), no other knob | the fixed row's base | ✅ |
| Q2 | ~~Real-CPU path: 2 MB forwarded, FB direct, byte order, landing order~~ **closed** except the FPGA byte-lane wiring of the direct FB window (A1, a 16-bit bar fill on metal confirms) | console/X performance | ✅ / A1 |
| Q3 | Presence signature: `0x1A0` = `0x0103`, test `(v >> 8) == 1`; `+0x000` reads `0xFFFFFFFF` so it must never be probed; `FW_ALFA` is a build counter — **the no-board/uninitialised limb stays UNSETTLED (= A4, bus/FPGA behaviour)** | `zz9k_present()`; the open-bus test | `0x0103` ✅; A4 ❓ |
| Q4 | ~~Framebuffer envelope~~ **closed**: `[+0x200000, +0x3000000)` is clear; first client above is GFXData `+0x3200000`; the 46 MB bound is the AmigaOS driver's, safe to reuse; the firmware writes there only for its boot splash | `fb_phys`, `fb_max`, `board_size` | ✅ |
| Q5 | Mode list: ids 5/17/18/19/20/21 are all 60 Hz HDMI timings (`video.c:108,120-124`) — **the demo monitor's lock is the only open part (A6, metal)** | console default; `Xzz9000` mode choice | table ✅, monitor ❓ |
| Q6 | ~~What is on the HDMI before any mode is set; is there a way back?~~ **closed** for the firmware: its own boot splash; one output; no native path (`video.c:245-270`, `rtg.c:1905-1908`). **A2 closed (2026-09-16):** the native console is the Amiga RGB output (what the RGB capture sees); the Z3660's RTG HDMI is behind a hand-flipped HDMI switch — the two are never on the capture at once, and enabling RTG cannot disturb the console the operator reads on RGB | "noconsole" = no-op; demo safety: RTG never takes the RGB console; metal recipe in blocks (§6 step 4) | ✅ |
| Q11 | Which layer sets the VM limit for the X server on metal — the X startup script (`ulimit -v`), the login class, or the kernel's rlimit default — and what the 060 kernel's default is | `Xzz9000` mmaps 46 MB on the Z3660 row | bench: shell default 16 MB soft, hard unlimited; metal ❓ |
| Q7 | ~~16-bit accesses to the register file~~ **closed**: whole aligned longword dispatched, no size decode (`rtg.c:583`, `:779,:1042`) — 32-bit only | `regw` | ✅ |
| Q8 | ~~The firmware "test area" `0x1000-0x1FFF`~~ **closed**: raw memory, **unconditional** printf per access, nothing else decodes there → excluded from the row's device space (`reg_size`/`fb_offset` 0x1000) | mmap/GETREG bound | ✅ |
| Q9 | ~~Under `autoconfig_rtg YES`, the product byte the ROM reports on metal~~ — **closed by the adversarial check**: `0x01` on the emulated path (`cpu_emulator.cpp:338-345`), the FPGA path (`z3660.v:981`) and the documented real-CPU board list (`docs/real-cpu-autoconfig-and-ext-rom.md:21,96-102`) | the REJECT row's autocon code | `0x144B0001` ✅ |
| Q10 | ~~Cache attribute expected from the 68k side~~ **closed**: firmware sets ARM-side only (registers non-cached, FB write-through, `defines.h:9,12`); 68k side: registers CI/serialized (the `REG1` handshake), FB CI or WT-with-flush — `DTT0` CI is correct | 060 metal correctness | ✅ |

---

## 6. Bench plan

All bench steps run on the kerntools piscsi rig via amix-kerntools, with the
2026-09-15 evidence recipe (D245 clean-gate sum, `bootinfo.autocon[]` decode, driver boot
lines, `zz9k_test` transcript, IPC `SCREENSHOT`, native console restored). Mechanics: `nohup`
tests through amixrun; ~0.4x wall clock with the window hammered.

| Step | Kernel / emulation | Proves | Pass |
|---|---|---|---|
| **0 regression** | golden 030 build of this repo, default rows — kerntools record repointed to `overlay/…` (c011498), **no `make=`**; `gfxcard_type=ZZ9000_Z2 gfxcard_size=4` | the table + accessor refactor changed nothing | boot lines as 09684 modulo wording; bars 1920x800; console test; `fb 0x10000+0x3d0000`; `MODE readback 0x0111` (ZZ9000 rows only — the Z3660's MODE is write-only, §3.3) |
| **1 32-bit window rehearsal — PASS 2026-09-16** (kernel 40227; report `2026-09-16-zz9000-rig-step1/31-report.md`; one finding: the process VM rlimit, §8) | same kernel + a **bench-only** row accepting ZZ9000 product 4 inside the identity map (record `make=ZZ9K_TARGET=-DZZ9K_BENCH_Z3` as run; from now on `make=ZZ9K_BENCH=-DZZ9K_BENCH_Z3`); amiberry `gfxcard_type=ZZ9000_Z3 gfxcard_size=128 z3mapping=uae` → `0x10000000`, `er_type 0x83`, FB `+0x10000` (`amiberry src/gfxboard.cpp:444-455`, `src/expansion.cpp:296-308`) | kernel dereference of a window at `0x10000000` (console, FILL, rw); user `mmap` above 16 MB with 2 KiB `phystopfn`; `board_size` capped by `fb_max` | bars + console; a `devmaptest`-style aliasing check on the mapping |
| **2 Z3660 hard-map rehearsal — PASS 2026-09-16** (kernel 40217, bytes c5a5a46, amiberry stand-in 814d25c; report `2026-09-16-zz9000-rig-step2/31-report.md`) | record `make=ZZ9K_TARGET=-DZZ9K_TARGET_Z3660,ZZ9K_BENCH=-DZZ9K_BENCH_Z3660` (the second define drops the combo REJECT row: amiberry's piscsi *model* AutoConfigures as `0x144B0001`, which on metal would mean `autoconfig_rtg YES` and rightly stop the probe before the fixed row); amiberry fork gains a **Z3660-RTG stand-in**: a thin bank at `0x10000000`, no AutoConfig (precedent `debugmem_bank`, `src/memory.cpp:1536,2755-2764`), that **translates the Z3660's 32-bit `0x100`-based registers onto the existing `zz9000.cpp` handlers**, FB at `+0x200000`, FW `0x0103`, no HW_VERSION, Z3660 mode ids — amiberry-agent work, cross-repo ask; rig must run `chipset=aga` for the gate | the fixed row end-to-end: gate, presence, no autocon entry, compacted device space, `zz9k_test` without its gate, X-style mmap | boot line `zz9000: Z3660 RTG at 0x10000000 …`; bars; console at the row's geometry; `GETAUTOCON` shows no RTG entry; mmap never reaches `+0x2000..` physical |
| **3 register fidelity** | as 2 after `FIRMWARE-RTG-INTERFACE.md` | §3.3/§3.4 verified against the doc; stand-in corrected where the doc differs | row values == doc |
| **4 metal — PASS** (candidate-2 kernel, metal session 2 rows 0–7 incl. OpenLook on the Z3660 HDMI; A4000D; `docs/DEMO-RTG.md`). *Plan text below as of 2026-09-16.* | **Rule (2026-09-16): RTG goes onto the demo machine only after the demo cards are fully ready.** 68LC060 Model-B kernel (`amix-040-060-port`, relink script + Model-B sysroot/converter); the Z3660 firmware side drives the A4000D **after 2026-09-19**, never the demo media. **Display topology (2026-09-16):** the RGB capture sees only the Amiga RGB output; the Z3660's RTG HDMI goes through an HDMI switch flipped by hand — RTG is visible only with someone at the switch, and while RTG is on the capture the native console is not (and vice versa). The recipe therefore runs in **blocks**: console-side steps (boot lines, `mkdev`, tool build, `zz9k_test` up to "pattern active") on the RGB capture, a switch flip, RTG-side checks (bars, console test, X), a flip back — and **every RTG-side check has a serial/telnet-visible confirmation** (driver boot lines, the `zz9k_test` transcript, a `dd`/`od` readback of the framebuffer, `Xzz9000`'s log) so the run is decidable without the frame | the real firmware; 4 KiB `phystopfn`; DTT0 CI; the runtime VM limit (§8) | driver boot line and transcripts on the RGB capture; bars on the RTG HDMI when the switch is flipped to it, with the framebuffer readback as the frame-independent proof; `Xzz9000` up per its log |

The amiberry `zz_set_pan` bug blocks the visual pass of every step until the fork fixes it
(`bench/zzpanfix.c` workaround meanwhile; the driver is not changed for it).

---

## 7. The native compiler constraint

**Bench:** GNU GCC **1.40.5** (68k, SGS/AT&T syntax), `/usr/sys/Makefile` `CC = gcc`, measured
(`31-report-resume.md`; kerntools `docs/zz9000-rig.md:75-81`). Upstream's C compiles clean;
only `-fno-builtin` (gcc 2.x) is rejected, stripped at push time. **Metal:** cross
`m68k-cbm-sysv4-gcc 2.7.2.3` + the port's Model-B sysroot (`amix-040-060-port/include-modelb/
sys/immu.h`, `param.h`) or its per-driver converter (`src/z3660_modelb.py`).

| Construct | In upstream? | 1.40.5 | 2.7.2.3 | Rule |
|---|---|---|---|---|
| K&R definitions, bare externs (`extern int autocon();`) | throughout | ok | ok | every new function K&R |
| ANSI prototypes, `void *` arithmetic, `enum`, `//`, `const`, `typedef` | none in the driver | — | — | do not introduce (the row struct uses `unsigned short *` etc.) |
| `UL` suffixes (`zz9000.h:14-16,52-54,86-87`) | yes | ok (compiled) | ok | fine |
| `-Dinline=__inline__`, `#include "sys/inline.h"` (`zz9000.c:15`) | yes | ok | ok | keep |
| `-fno-builtin` | Makefile | **rejected** | ok | leave upstream's Makefile; kerntools strips it |
| `-traditional` | Makefile | ok | ok | the cross kernel build uses `-D__STDC__=0` instead because `<sys/immu.h>` has `void` prototypes (`amix-kerntools/build-cross-kernel.sh:544-548,636-640`) — a flag-set matter for the metal script |
| GNU inline asm, `register … __asm__("d0")`, clobbers | **none** | rejected (`amix-z3660scsi` d90f5fd: `invalid register name 'd0'`) | ok | never add; `VPOSR` needs none; cache asm → `.s` |
| static `unsigned char` array initialisers (`zz9000.c:528-573`) | yes | ok | ok | fine |
| `spl6/splx`, `bzero/memset` | not used | — | — | keep it that way |
| `phystopfn()` (`zz9000.c:1246`) | yes | `pa >> 11` (2 KiB) | per the sysroot's `PNUMSHFT` | **the trap**: correct on the bench kernel; on Model-B it must compile against the port's `immu.h` or be converted, else `mmap` lands at 2×pa. Never hand-roll the shift; size nothing in pages |
| 32-bit volatile MMIO (`*(volatile unsigned long *)`) | not in upstream; both siblings use it (`z3660.c:232-233`) | ok | ok | the Z3660 row's accessor |

---

## 8. Risks

| Risk | Retired by |
|---|---|
| Register file differs (this document's central finding) — the port is a register-map port, not an identity swap | the accessor + per-row `regoff`; bench step 2's translating stand-in; step 3 against the firmware doc; step 4 |
| A 16-bit access leaks into the Z3660 path | `regw` is the only width source; GETREG/SETREG use it too |
| Framebuffer envelope shared with other clients | §3.2 layout + `fb_max = 0x2E00000` (the AmigaOS driver's own bound); Q4 |
| Userland RW over the piscsi mailbox/bounce via `mmap(board_size)` | compacted device space (§4.4) |
| Userland or GETREG loop over the firmware test area stalls the RTG thread on UART printfs | the row's device space and `reg_size` end at `0x1000` (§4.4) |
| Presence test false-negative (`FW_VERSION` numbering) or false-positive (open bus) | row-specific test; compile-time gate; AGA gate |
| No 1920x800 → console `ENXIO`, `Xzz9000` mode failure | per-row mode table + console geometry; Q5 |
| Capture switch is a stub: "noconsole"/`SETSWITCH CAPTURE` cannot give the display back | `NOCAPTURE`; Q6 decides what "restore" means on metal; nothing runs from `init_tbl[]` |
| 060 copy-back cache over the framebuffer | DTT0 CI on the low 1 GB (`z3660.c:688-709`); assert at attach like the siblings |
| 060 4 KiB `phystopfn` | Model-B sysroot / converter; aliasing check |
| **A process cannot `mmap()` the window under Amix's default per-process VM limit** — the login shell's `RLIMIT_VMEM` soft default is 16 MB (`ulimit -a` memory(kbytes) 16384; hard unlimited): `mmap(board_size)` of the 47 MB bench window returned ENOMEM, every size ≥ 16 MB failed and ≤ 12 MB succeeded; with `ulimit -v 131072` the same kernel mapped all 23 584 pages and the bars drew (step 1, `23-mmap-size-probe.txt`). Never visible on a real ZZ9000 (4 MB window) | **runtime contract**: whatever opens `/dev/zz9000` and maps `board_size` — `Xzz9000` (46 MB on the Z3660 row), `zz9k_test` — must run with `RLIMIT_VMEM` ≥ its own VM + `board_size`: `ulimit -v` in the X startup script, or a raised kernel rlimit default (Q11). `zz9k_test` restores the display and prints the hint on an mmap failure. Upstream-relevant for isoriano's Zorro III users |
| The demo machine's display is taken over by a bench-minded test | metal only after 2026-09-19 via the Z3660 firmware side; no `S99zz9000` on the demo media *(2026-09-16 wording)* |
| amiberry pitch bug masks regressions | **fixed in the amiberry fork (71e7527)**: step 2 rendered with X2=0/PAN=0 and no workaround; `zzpanfix.c` is no longer needed on the current fork |
| `read(2)` at or past `board_size` returns ENXIO rather than a 0-byte EOF | upstream's `zz9000rw` semantics, kept: a device window is fail-closed; documented in the recipes |

---

## 9. Decisions (project, 2026-09-15)

1. **ACCEPTED — device identity stays `/dev/zz9000`, major 49, ABI unchanged**: one driver, two
   rows, `Xzz9000` unchanged. (The grimoire's major registry presumes a separate driver; it
   is to be corrected.)
2. **ACCEPTED — switch shape = option C**: board table + compile-time row selector
   `-DZZ9K_TARGET_Z3660`, no `#ifdef` inside functions; the default build reproduces upstream
   behaviour, bench step 0 is the proof.
3. **ACCEPTED — compacted device space on the Z3660** (`fb_offset 0x2000`, `fb_phys 0x200000`).
4. **Console geometry for the Z3660 row: default 1920x1080 (id 5)**; whether
   the demo monitor wants 1280x800 is open — the console mode stays a one-line table value.
5. **Bench-only product-4 row (`-DZZ9K_BENCH_Z3`) kept** as a documented bench aid until the
   step-2 stand-in exists; revisit then.
6. Cross-repo asks, filed with the project: kerntools is repointing its chardevs record to
   this repo (the old sibling `zz9000-amix` path aborts every build) — **the target build is the
   record's `make=ZZ9K_TARGET=…` pass-through** (kerntools c011498, §4.2), one record per build;
   the amiberry pan-pitch fix and the Z3660-RTG stand-in bank wait for review of
   the amiberry merge; the Z3660 firmware side gets Q1–Q10 (`docs/FIRMWARE-RTG-INTERFACE.md`); the
   040/060-port relink question waits until after 2026-09-19.
7. §0 and §3.3 are written so that the register-file finding can go to isoriano as-is.

---

## 10. Assumptions I could not verify from source

- That the real-CPU (68060) framebuffer window from `+0x200000` is byte-order-transparent for
  16-bit and 32-bit stores, and that the FPGA has landed a register write in DDR before it
  raises REG1 (`rtg.c:557`) — both are FPGA behaviour, not visible in C (`z3660.v` was read
  only for the autoconfig nibbles).
- Which physical panel the demo operator watches (the Z3660's HDMI shows the firmware splash, then RTG; the native Amiga screen has no path through the Z3660) (A2).
- That reading `0x10000000+0x1A0` on an AGA machine *without* a Z3660 bus-errors rather than
  returning open-bus data (the reason the row is compile-time gated; not testable here).
- The demo monitor's accepted modes (Q5).
- That the Model-B kernel's `segdev` mmap path accepts a device window at `0x10000000` (the
  port's `devmaptest` covered `/dev/mem`; the VA2000 driver covered a Zorro II window).

---

## 11. RTG in the demo — what it would take on the real 060 (2026-09-16 assessment)

The rule of §6 step 4 stands. This section defines the work so that it can be judged. Facts from `amix-040-060-port` are
cited; the 68040/68060 port's answer to the A7 question is merged where it differs.

### 11.1 The kernel

The demo runs a 68LC060 Model-B kernel from the port's relink machinery: the baseline
`68060-260812-06` is `relink-040.sh` on the stock `stand/unix` (`STATUS.md:48,67`); the "r13"
in use is the FPE round-13 lineage `unix-060-fpe-r13-CARD1` (buildid `68060-260828-50`,
`docs/contracts/FPE-R10-VEC60.md:1226-1247`; the kernel `zen0` was verified on 2026-09-09).
**Any RTG kernel is a NEW kernel** = that base + this driver, produced by a new relink script
in the port's style; it is not the kerntools 030 pipeline (whose on-box `ed` wiring is
030-only in execution).

### 11.2 The relink shape for a `cdevsw` + `mmap` char driver (A7) — measured in r13 by the 68040/68060 port

Source: the 68040/68060 port's answer, `2026-09-15-rtg-relink-brief/ANSWERS.md` (every claim
with a `file:line`; the r13 artifact is the archived master `unix-060-fpe-r13-CARD1-ced0s1-metal-green-2026-08-28`).

| Step | Fact (r13, measured) | For zz9000 |
|---|---|---|
| Row 49 | **free**: `cdevsw` @ `.data 0x9dd4`, 70 rows × 52 B; row 49 @ `0xa7c8` = ten `R_68K_32 nodev` at +0..+0x24, no reloc at +0x28/+0x2c, `nullflag` @ +0x30 — the same shape as rows 68 (VA2000) and 51 (zen0) | `patch_va2000_cdevsw.py` with `CDEVSW_MAJOR=49`, `EXPECT_SLOT_OFF=0xa7c8`: retargets `d_open..d_mmap`, all-or-nothing |
| Init | `init_tbl` (0x9308) and `io_init` (0x99a4: `parinit`, NULL) have no spare slot; r13's `parinit` is **already the z3660 wrapper** (`000f7510 T parinit`, `0000fe6c T parinit_orig`), so the VA2000 script's `PARINIT_ADDR=0xfe6c` aborts on it | chain a second wrapper the FPE way (`relink-040-fpe.sh:175,187`): `--weaken-symbol parinit --add-symbol parinit_z3660=.text:0xf7510`, link `jsr zz9000init; jmp parinit_z3660; .balign 4` last → io_init → zz9000 → z3660 → stock. This runs at **io_init time, before `init_tbl`** — fine: `zz9000init` needs only `bootinfo` (loader-filled), `VPOSR`, the board registers and `printf` |
| Where | r13 = `relink-040.sh → -z3660.sh → -f4arms.sh --ufault --cache 60 → -f6.sh --kvd --swa (= build/unix-040-f7vs, pinned, sha d43a59cc…) → -fpe.sh → stamp-card1 → stamp-cputype --set 60`; FPE is always the last link | the RTG pass sits **between f6 and fpe, starting from the pinned f7vs** (a fresh `relink-040.sh` today carries change D and does not reproduce r13's base) |
| Compile | `AMIX_KERNEL_CFLAGS` with `-m68020→-m68040`, `AMIX_SYSROOT=build/sysroot-modelb`, `-traditional` (no `-D__STDC__=0` anywhere), then `check_page_geometry.sh`; `scan060.py` by hand (no script gates it): `-O` turns `x / CONST` into a 64-bit `mulsl` → vector 61 in supervisor mode | **done and clean** (6137966): compiled exactly so; `check_page_geometry.sh` → one shift by 12; `scan060.py` → 0 hits (the previous object had two `mulsl #1717986919` from the console test's `/10`, now a loop) |
| Symbols (`nm -S` r13) | `autocon` T, `bootinfo` D 1900 B, `conputc` **D 4 B** (function-pointer variable → `coputc`; readers `putchar`, writers only `bbopen/bbclose`; `consinit` never touches it, so a runtime assignment persists), `copyin` T, `copyout` T (the port's wrapper, intended), `printf` T, `uiomove` T — **`builtinfont` ABSENT** (the kernel has `scr_builtinfonts` → local `font0base/font1base`; `sunfont` absent); the leak check fails the link on any `U` | `-DZZ9K_NO_BUILTINFONT` (6137966) drops the extern: the object's U set is exactly {autocon bootinfo conputc copyin copyout printf uiomove}; the console uses its built-in glyphs (a font object can be added later) |
| Link | the `.o` on the `ld -r` line, closer object last; guards: `check_relink_relocs.py`, `patch_b2_flip --check`, `stamp_buildid`; `unix_boot040` mandatory (330 PC-rel relocs) | a cloned `relink-040-zz9000.sh` |
| Window sharing | the RTG register page `+0x000..+0xFFF` **contains the ethernet driver's register block `+0x104..+0x1AB`**; FB `+0x200000..+0x2FFFFFF` overlaps nothing (piscsi `+0x2000/+0x80000`, eth frames `+0x7ED0000`); the 128 MB window spans the eth ring | the Z3660 row never writes `0x104`, and from 6137966 its userland device space is **the framebuffer alone** (no register page mapped; GETREG/SETREG only, root, `0x1000`-bounded); mmap offsets are bounded to `FB_MAX` by construction |
| Cache | `DTT0 = 0x003fc060` = 0–1 GB CI for kernel VAs (`pstart040.s:326-327`); a **user** mmap of the FB is outside DTT0 → leaf-PTE mode; r13 has no `hat_cm_fb_add` (change D absent) → default NCS serialised: coherent, slow | acceptable for the demo (CPU fills of 4 MB frames); note ISSUE-49 (segdev calls `d_mmap` twice per 4 KiB page, second wins — harmless here) and ISSUE-47 (a map of a non-responding address retries forever — the presence test runs first) |
| `RLIMIT_VMEM` | r13 `rlimits` (D 0x9390): VMEM {16 MiB soft, 0x7fffffff hard}, byte-identical to stock; a device mmap counts (`spec_segmap → as_map`, ENOMEM); `RLIMIT_DATA` also 16 MiB (gates `brk` for an X heap) | tools: `zz9k_test` now raises its own soft limit (`setrlimit`, libc `sysent[128]`); `zz9k_demo` also `ulimit -v`; for X: `ulimit -v` in its startup, or a 4-byte stamp at `rlimits+0x30` (a `stamp-cputype.py`-pattern tool, a first for the port) |

### 11.3 What it would show, and what it cannot

- **Can, on a relinked r13 + driver:** the RTG HDMI at 1920x1080 (or 1280x800): the bar and
  fill/copy pictures and the console test page (`zz9k_demo`); **any command's output on the RTG
  screen** through `zz9k_cat` (`ZZ9KIOC_CONPUTS`, no kernel hook); kernel messages on RTG through
  the `conputc` redirect (the kernel `printf` funnel — userland VT writes do not pass through it).
- **A real login shell on the RTG screen** is reachable without console source, per the 68040/68060
  port: 2.1c has no `c1write`; the function at upstream's hook position is `scrcon_put(cp,p,n)`
  (`00005284 T`, byte-identical to stock, called from `getoblk` for every VT write and from
  `coputc`), and the port's sanctioned override (`--weaken-symbol scrcon_put --add-symbol
  scrcon_put_orig=.text:0x5284`, a ~10-instruction first-party asm wrapper: load `cp->screen`,
  compare `displayedscreen`, `jsr zz9000consolewrite(p,n)`, `rts` if nonzero else `jmp
  scrcon_put_orig`) binds both callers; keyboard stays native. Not built; an inference marked
  as such by the 68040/68060 port.
- **No X** (`Xzz9000` is blocked on the native compiler). *(True on 2026-09-16; since superseded — cross-built 2026-09-18, `docs/X-CROSS.md`, and run on metal, `docs/DEMO-RTG.md` row 7.)*
- **Display topology** (§6 step 4): RTG on the hand-flipped HDMI; RGB console on the RGB capture.

### 11.4 Effort, re-validation, and what changes on the card

- **Effort (68040/68060 port):** the pass itself — a cloned `relink-040-zz9000.sh` (nm-read weaken
  chain, the two-constant cdevsw patcher, closer last, existing guards) — **is a day**, given a
  driver that compiles clean against `include-modelb` with `scan060` clean (now the case); **the
  long pole is metal**.
- **Re-validation:** `docs/ACCEPTANCE.md` has no "one more driver" clause; the applicable §3 row
  is "glue a variant links — device code → host gates + a boot of that variant + a test aimed at
  what the glue does", plus §4 host gates (`TOTAL complaints: 0`), boot identity + every magic
  word (they move when `.text` changes, FPE-R10-VEC60 §13.9), §5 the historical-eleven battery
  (≈90 s–2 min), §6 the 060 rows (`fp060probe 7/7`, `ftest060`, `isp61ea 7/7`, `fpenab060`,
  `fputest060 fork`), the r13 RESULTS rows the demo relies on, the multiuser band, and the §8
  client on the RTG. `docs/PLATFORM-LC060-Z3660.md` caps real-CPU runs at 15 min and forbids
  unattended boots. **Calibration:** r13 itself never ran §5/§7/§8 on the LC060 — its metal
  acceptance was one 40-minute session on 2026-08-28 plus later boots to multiuser; "more than
  r13 did" is a separate decision. kerntools' floor (`nm -u` clean, boot, attach line, `mkdev`, one
  mode-set + bars) is the minimum the driver side would ask for.
- **On the card:** the new kernel (a second kernel file or a separate medium), `/dev/zz9000`
  (`mkdev.sh`), the tools under `/usr/amiga/bin`. No preset or firmware change (`autoconfig_rtg
  NO` is what the driver wants; ZTop/firmware on 0: stays F492BEC1).

### 11.5 What the metal probe of 2026-09-16 retires before any kernel exists

**Metal fact (run 1, A4000D, r13, preset 7):** a user-space `mmap()` of `/dev/mem` at physical
`0x08000000` returned `2f004eb9` where `lseek+read` of the same longword returned `46fc2700`;
the probe's self-test refused the mapping and the card was never touched (the HDMI stayed on the
firmware's 800x600 splash — Q6's "before" answered). **Mechanism (68040/68060 port, by disassembly of
four kernels):** the port's ISSUE-46 — `mmmmap` computes `pfn = (offset + 0xfff) >> 12` and the
retained 2 KiB segdev stepping calls `d_mmap` twice per 4 KiB page, so every `/dev/mem` mapping
lands one page high (`2f004eb9` = `.text+0x1000`; `KNOWN-ISSUES.md:4410-4470`,
`REALHW-Z3-APERTURE-PROBE-260819.md:14-22` recorded the identical pair before the fix). The fix
(`patch_devmmap2.py`, 4 bytes in `mmmmap`) is in the fresh `relink-040.sh` base but **not in the
pinned f7vs, r13 or the candidate**. `/dev/zz9000` never goes through `mmmmap` (`zz9000mmap`
truncates via `phystopfn`, like the unaffected `scrmmap`/`va2000mmap`), so this is **neutral** on
the driver's own mapping. The mapping's cache class is **NCS** on r13 / candidate 2 / candidate 3
(no `hat_cm_fb` table in those binaries; unmanaged device pages take `Lcm_dev` = `oril #0x40`;
`hat_cm_ram` is for managed pages only — 68040/68060 port, `HAT-CM-FB-R13.md`, 2026-09-27, correcting
the earlier "deferred TODO" reading). NC is possible only on the driver-line base, by registering
the window with `hat_cm_fb_add` at attach (P-5 fix 5). `/dev/mem` mmap is not an oracle on any r13-lineage kernel; the 68040/68060
port recommends leaving r13's bytes alone for the demo. It **raises the bar for validating
`zz9000mmap` on the metal**: the first thing
a relinked kernel must show is a `devmaptest`-style aliasing check on `/dev/zz9000` (map the same
page twice and two adjacent pages, compare with `read(2)` of the same offsets) before `zz9k_test`
draws through the mapping — `tools/zz9k_maptest` (d85c037) is that check, and `zz9k_test … rw`
draws through `write(2)` so the pictures never depend on the mapping. The same binary ran the whole sequence on the amiberry stand-in
(signature, mode, bars, 16-bit lanes `0000/0103`, `f800f800` row readback), so A1 stands on
emulation. Run 2 (`-r`: every access an `lseek`+`read`/`write` on `/dev/mem`, kernel-executed)
**passed on the metal** at 1920x1080 and 1280x800: signature `00000103` (the driver's first
access, kernel-executed), framebuffer write/read self-test `MATCH`, the mode-set sequence
acknowledged by the firmware's serial (`video_mode_init: 5 … 1920x1080 16 bit color, 60 Hz`;
`17 … 1280x800`), `f800f800` row readback, bars/blue/bars on the Z3660 HDMI (frames). A1 (byte
order of 16-bit stores on the FPGA path), A6 (both modes lock) and Q6 (the firmware splash
before) are answered on metal; see `docs/DEMO-RTG.md`.


`tools/zz9k_memprobe` (root, `/dev/mem`, no driver) exercises on the real LC060 exactly the
accesses the driver's Z3660 row makes: the 32-bit signature read at `0x100001A0`, the 16-bit
lane reads (A1), the mode-set write sequence, 16-bit framebuffer stores at `+0x200000` and
their byte order, and the HDMI output (A6, Q6). If it draws the bars, the driver's metal risk
reduces to the relink mechanics of §11.2. Result: see `docs/DEMO-RTG.md`.
