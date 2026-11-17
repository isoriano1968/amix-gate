<!-- Verbatim copy of 2026-09-15-z3660-rtg/FIRMWARE-ANSWERS.md as delivered by the Z3660 firmware side on 2026-09-15 (firmware branch amix-main-2, source-only, no metal). Kept in-repo as evidence; the condensed merge is docs/FIRMWARE-RTG-INTERFACE.md. Out-of-repo path prefixes shortened to the evidence dir name (2026-10-02); text otherwise verbatim. -->

# FIRMWARE-ANSWERS.md — Z3660 RTG firmware-interface answers

Answers to `amix-z3660rtg/docs/FIRMWARE-RTG-INTERFACE.md`, derived **from firmware source only**
(branch `amix-main-2`), for a driver author porting `isoriano1968/zz9000-amix` to the Z3660.
Every claim carries a `file:line` cite. No metal was touched; questions whose only remaining
evidence is a live read are marked as such.

Path convention (same as the sheet):
`FW/` = `Z3660/z3660-firmware/Z-TURN/vitis_ide/`.
ZZ9000 driver header cited as `DRV-zz9000.h` =
`2026-09-14-isoriano-repos/zz9000-amix/overlay/usr/sys/amiga/driver/zz9000/zz9000.h`.
Amiberry emulation cited as `amiberry/src/…`.

---

## HEADLINE FINDING FOR THE PORTER (read first)

**The Z3660 firmware does NOT use the ZZ9000 register map your `zz9000.h` targets.** Your header
is the *classic MNT ZZ9000* 16-bit map (MODE `0x02`, CONFIG `0x04`, FW_VERSION `0xc0`, blitter
`0x10–0x32`, reg window `0x2000`, FB at `0x10000`). The Z3660 firmware implements a **different,
32-bit-word register map** (`FW/Z3660/src/rtg/zzregs.h`): MODE `0x100`, CONFIG `0x104`,
FW_VERSION `0x1A0`, blitter `0x110–0x14C`, register window = **first 2 MB**, framebuffer at
**`+0x200000`**. Identity is **mfg `0x144B`, product `1`, Zorro III 128 MB** — not ZZ9000's mfg
`0x6d6e` / product 3(Z2)/4(Z3). Only the MODE-word *encoding* and the color-mode ids are shared.
So this is not an identity swap — it is a **new register backend**. See the comparison table under
Q-register-map and the emulation analysis at the end.

---

## Q1 — The hard-map at `0x10000000` under `autoconfig_rtg NO`

**Answer: CONFIRMED, and gated by exactly one knob (`autoconfig_rtg`), in both boot modes.**

- The gate is `z3_enabled & 2`, and that bit *is* `autoconfig_rtg`:
  `shared->z3_enabled |= config.autoconfig_rtg ? 2 : 0;` (`FW/Z3660/src/main.c:1694`).
- **Emulated boot modes (core1 UAE):** when `(local.z3_enabled & 2) == 0`, `reset_autoconfig()`
  plants `autoConfigBaseRTG = 0x10000000` and marks `configured_z3 |= 2` (already-configured),
  then maps the banks (`FW/Z3660_emu/src/cpu_emulator.cpp:221,224,226`; log at `:232`).
- **Real-CPU boot mode (68060 / MOBOCPU):** `DISABLE_RTG_AUTOCONFIG` is issued and the ARM sets
  the RTG cache window at megabyte index `ini = 0x100` → `0x100<<20 = 0x10000000`
  (`FW/Z3660/src/main.c:1436,1440,1662-1665`). Same fixed base for the real bus.
- **No other knob moves it.** `amix_mode`, `amix_ram`, `autoconfig_ram` only shape the CPU-RAM
  window *below* `0x10000000`; both the amix and non-amix map branches lay down the same default
  `RANGE_MAP(0x1000,0x1800,dmmy_bank)` for `0x10000000–0x17FFFFFF`
  (`FW/Z3660_emu/src/uae/uae_emulator.cpp:1225`, else-branch `:1223`), which `init_rtg_bank()`
  then overlays with the RTG banks. The amix RAM window is explicitly clamped so it "can never …
  run into the combo board window at $10000000" (`uae_emulator.cpp:1180-1194`).
- When `autoconfig_rtg YES` (`z3_enabled & 2`) the base is *not* fixed: the guest's AutoConfig
  write assigns it — `autoConfigBaseRTG = data & 0xFFFF0000` (`cpu_emulator.cpp:549`); FPGA path
  enables Zorro III autoconfig (`main.c:1431-1434`). See A3 for where it lands.

Your `fixed_base = 0x10000000` and the AGA-gated probe (read there with no AutoConfig evidence) are
correct for `autoconfig_rtg NO`.

## Q2 — The real-CPU (68060) bus path for the window

**Answer: (a) yes, (b) yes, (c) big-endian/transparent, (d) yes — all confirmed.**

The real CPU's accesses to the window are captured by the FPGA and handed to the ARM's
`rtg_thread()` via the `REG1` gpio word (`FW/Z3660/src/rtg/rtg.c:552,559`). `zaddr = gpio & 0x1FFFFF`
(`rtg.c:570-571,644`) — so **only the first 2 MB is delivered to the ARM**; nothing in `0x000000..0x1FFFFF`
is decoded by the FPGA alone. Decode inside the 2 MB:
- `0x000–0xFFF`  → RTG registers (`write_rtg_register`/`read_rtg_register`, `rtg.c:583,681`)
- `0x1000–0x1FFF` → "test area" raw memory (see Q8)
- `0x2000–0x5FFF` → piscsi / pifloppy mailbox (`rtg.c:588-616`)
- `0x6000`+, `0x80000` → boot ROM / SCSI no-DMA

**(a) yes** — every register/mailbox access in the 2 MB is forwarded; the FPGA does not self-serve it.
**(b) yes** — the framebuffer at `+0x200000` and up is **NOT** handled by `rtg_thread()` at all; it is
a direct DDR window. The emulated `rtg_bank` is `ABFLAG_RAM | ABFLAG_DIRECTACCESS`, allocated 128 MB,
`baseaddr = RTG_BASE` (`FW/Z3660_emu/src/uae/uae_emulator.cpp:972,984-990`); mapped
`[base+0x200000, base+0x8000000)` (`init_rtg_bank`, `uae_emulator.cpp:1019-1023`). For the real CPU
the FPGA routes those accesses straight to DDR — no per-access ARM handling.
**(c) big-endian transparent** — on the ARM forwarding path register/longword values are `swap32()`'d
and words `swap16()`'d out of the DDR staging cell (`rtg.c:570,583,602,655`), i.e. the 68k sees
native big-endian; the swap only reconciles the ARM's little-endianness. The direct framebuffer
window is the same convention (the emulated `swap16/swap32` is the model of the FPGA behavior).
**(d) yes** — the ARM reads the write payload back out of DDR (`swap32(*address)`, `address =
RTG_BASE + zaddr`, `rtg.c:570,583`), which means the FPGA has landed the datum in DDR *before* it
raises `REG1`.

Metal-only residue: the FPGA byte-lane behavior for the direct FB window (A1) is not in C; the model
above is the emulator's and the ARM path's. A `zz9k_test`-style 16-bit bar fill on metal remains the
confirming read.

## Q3 — The presence signature

**Answer: `REG_ZZ_FW_VERSION` (`0x1A0`) returns `0x0103`; test `(v >> 8) == 1` is correct.**

- Format is `(REVISION_MAJOR << 8) | REVISION_MINOR` (`FW/Z3660/src/rtg/rtg.c:805-807`), with
  `REVISION_MAJOR 1`, `REVISION_MINOR 3` (`FW/Z3660/src/version.h:1-2`) → **`0x0103`**. Your assumed
  value and mask are exact.
- `REG_ZZ_FW_BETA` (`0x280`) = `REVISION_BETA` = **21**, a compile-time constant, stable
  (`rtg.c:809-810`, `version.h:4`). `REG_ZZ_FW_ALFA` (`0x284`) = `revision_alfa` = `REVISION_ALFA` =
  **0** in-tree (`rtg.c:813-814`, `FW/Z3660/src/alfa.c:3`, `FW/Z3660/src/alfa.txt:1`); it is a build
  counter — treat it as informational, not a presence gate.
- **Read at `+0x1A0`, RTG mapped:** `0x0103`. **Read at `+0x000`, RTG mapped:** there is no register
  at `0x000` (no HW_VERSION), so `read_rtg_register()` falls to `default:` and returns
  **`0xFFFFFFFF`** (`rtg.c:1024-1026`). Do NOT use `+0x000` as a probe — its "present" value is
  indistinguishable from open bus. Probe `+0x1A0` and test `(v >> 8) == 1`.
- **RTG disabled / not yet initialised / no Z3660:** not resolvable from C. Under `autoconfig_rtg NO`
  the bank is mapped extremely early (at CPU reset, `cpu_emulator.cpp:117,137`→`reset_autoconfig`),
  so by guest-boot it is live. On a machine with no Z3660, or before the FPGA decodes the window, the
  return (open-bus `0xFFFFFFFF` vs bus error) is FPGA/bus behavior — see A4. This is the one part of
  Q3 that source cannot settle.

## Q4 — The framebuffer envelope

**Answer: the firmware maps 126 MB of DDR from `+0x200000`; `[+0x200000, +0x3000000)` (46 MB) is
clear of every other client; the 46 MB bound is the AmigaOS driver's choice, not the firmware's.**

- Framebuffer base is `FRAMEBUFFER_ADDRESS = RTG_BASE + 0x200000` (`FW/Z3660/src/memorymap.h:16,18`).
- The firmware maps the DDR window `[+0x200000, +0x8000000)` — i.e. up to **128 MB board / ~126 MB of
  FB+aux** — via `init_rtg_bank()` (`uae_emulator.cpp:1023`, `rtg_bank` allocated `0x08000000`,
  `uae_emulator.cpp:984-985`). The 46 MB (`+0x3000000`) ceiling is nowhere in firmware; it is the
  AmigaOS driver's own bound (`DRV/rtg/gfx.c:511` per the sheet). Safe to reuse — see the client map.
- Client offsets relative to `RTG_BASE` (`memorymap.h`):
  - boot ROM `+0x6000` (`:37`), SCSI no-DMA/bounce `+0x80000` (`:36`) — both **below** `+0x200000`,
    inside the register window, not in the FB.
  - piscsi mailbox `+0x2000..+0x5FFF` (`rtg.c:588`, `SCSI_ADDR_MAX 0x6000`) — also in the reg window.
  - GFXData / `Z3_SCRATCH_ADDR` `+0x3200000` (`:21`) — first client **above** the 46 MB bound (50 MB).
  - soft3D `+0x4200000` (`:24`), USB `+0x7F00000` (`:35`) — well above.
  - Note: `AUDIO_TX/RX` (`0x07CE0000`) and ethernet BD/frame lists (`0x07E00000..`, `:28-34`) are
    written in the header as **absolute low-DDR** addresses, not `RTG_BASE`-relative — they do not sit
    in the `RTG_BASE` FB region as literally spelled. (The ARM MMU map does reserve `RTG_BASE`-relative
    MB `0x60-0x80` for overlay/audio/mpeg/eth, `cpu_emulator.cpp:432-459` — i.e. the top ~32 MB of the
    128 MB board. Those are all **above** the 46 MB FB bound.) Either way `[+0x200000,+0x3000000)` is
    clear.
- Does the firmware itself write into the FB range? **Yes, but only its own boot screen / console**
  (`video.c`, see Q6) — it renders into the *same* framebuffer before the guest sets a mode; once the
  guest owns the mode it owns the FB. There is no concurrent firmware writer in that range during
  guest operation.

So `fb_phys = base+0x200000`; `fb_max` up to `base+0x8000000` is firmware-backed, and the driver's
46 MB choice is safe.

## Q5 — Modes the demo monitor accepts

**Answer: the mode ids you list are Z3660-correct and all are 60 Hz HDMI in the table; monitor
acceptance itself is a metal read (A6).**

The Z3660 mode enum (`FW/Z3660/src/rtg/zz_video_modes.h:3-28`) and timing table
(`FW/Z3660/src/video.c:101-126`) give, all with `vhz = 60`, `hdmi = 1`:
- `5` = `ZZVMODE_1920x1080_60` → 1920x1080@60 (`zz_video_modes.h:9`, `video.c:108`)
- `17` = `ZZVMODE_1280x800`  → 1280x800@60  (`:21`, `video.c:120`)
- `18` = `ZZVMODE_1920x1200` → 1920x1200@60 (`:22`, `video.c:121`)
- `19` = `ZZVMODE_1600x900`  → 1600x900@60  (`:23`, `video.c:122`)
- `20` = `ZZVMODE_1680x1050` → 1680x1050@60 (`:24`, `video.c:123`)
- `21` = `ZZVMODE_1366x768`  → 1366x768@60  (`:25`, `video.c:124`)

Each `video_mode_init` programs the pixel clock and the SiI9022 HDMI transmitter
(`video.c:232-233`). `1920x1080` (mode 5) is a first-class 60 Hz HDMI mode in the table — your
default is sound. **Caveat:** whether the *demo monitor* locks each timing is not a source fact
(A6). Note `MNTVA_COLOR_15BIT` is remapped to 565 internally (`rtg.c:1082-1084`).

## Q6 — Who owns the HDMI output

**Answer: the firmware owns the single HDMI output; before an RTG mode it shows its own boot screen;
there is no capture / scan-convert path back to a native Amiga screen — `OP_CAPTUREMODE` is a stub.**

- At boot the firmware drives HDMI itself: the `BOOT_SCREEN` block picks
  `config.bootscreen_resolution` (1920x1080 / 1280x720 / 800x600), calls `video_mode_init(... ,
  MNTVA_COLOR_16BIT565)` and renders into the framebuffer (`FW/Z3660/src/video.c:245-270`; e.g. it
  draws "Z3660 reset..." text, `video.c:698`). So the HDMI shows the **firmware's own console /
  splash**, not a scan-converted Amiga screen.
- When the guest writes `MODE` (`0x100`), `video_mode_init()` reprograms the *same* HDMI output to the
  guest's RTG mode (`rtg.c:1111-1132`). One output, shared between firmware boot screen and RTG.
- `OP_CAPTUREMODE` (`0x30C`) is a **stub** — the `zz_set_monswitch(!zdata)` call is commented out
  (`rtg.c:1905-1908`), and `zz_set_monswitch` is not invoked anywhere. `REG_ZZ_MONITOR_SWITCH`
  (`0x318`) is only a *preset config field* (`rtg.c:867-869,2093-2096`), not a live video switch.
  There is no firmware path to display the native Amiga (AGA) screen through the Z3660. `main.c` calls
  `monitor_switch(1) // 1=RTG` once at init (`main.c:1651`).
- Consequence for the driver: `SETSWITCH CAPTURE` / "noconsole" has no firmware effect — treat the
  switch as a no-op (your assumption holds). Which physical display the operator watches during the
  demo is a metal observation (A2).

## Q7 — 16-bit register accesses from the real CPU

**Answer: CONFIRMED — registers are handled as aligned 32-bit words; use 32-bit accesses only, like
`amix-z3660scsi`/`amix-z3660net`.**

- Real-CPU / FPGA path: for `zaddr < 0x1000` the write path unconditionally reads the full longword
  from the DDR staging cell and dispatches the whole register — `zdata = swap32(*address);
  write_rtg_register(zaddr, zdata)` (`rtg.c:583`); there is no size decode for the register region.
- `write_rtg_register()` / `read_rtg_register()` mask the address to 32-bit alignment:
  `address = zaddr & 0x1FFFFC` (`rtg.c:777` read, `rtg.c:1050` write) and switch on the aligned
  offset. A read returns the full 32-bit register (`rtg.c:681-684`); the FPGA hands the CPU back the
  lane it asked for.
- Emulated path is identical: `read_rtg_register(add & 0x1FFFFC)` then the requested byte/word lane is
  extracted (`FW/Z3660_emu/src/cpu_emulator.cpp:981-985`).
- Therefore a 16-bit write into a register writes the **whole aligned 32-bit register** with whatever
  else sat in that longword — exactly the hazard the siblings avoid. Your `regw` must issue aligned
  32-bit accesses only. (Note: `write_rtg_register` even logs "write unaligned" on `zaddr & 3`,
  `rtg.c:1051`.)

## Q8 — The `0x1000–0x1FFF` "test area"

**Answer: functionally harmless (raw DDR), decoded nowhere else — but there is an UNCONDITIONAL
`printf` per access; a userland loop over that page will be throttled by ARM UART speed.**

- `0x1000–0x1FFF` is a raw memory area: reads/writes go straight to `RTG_BASE + zaddr` with a
  `swap`, and each access does a `printf` on the ARM UART (`rtg.c:573-577` write, `rtg.c:646-673`
  read). Those printfs are **not** gated by `debug_console` — they always fire.
- Nothing else decodes there: it is a distinct branch above the register region and below the SCSI
  mailbox (`rtg.c:573,646`), the same in every preset.
- **Real hazard:** the actual registers live in page `0x000` (`0x100–0x8xx`), and their accessors do
  *not* printf unless `debug_console.debug_rtg` is set (`rtg.c:782-784,1044-1046`). But if userland
  `mmap`s and pokes page `0x1000` in a loop, every access blocks the RTG thread on a UART write —
  that will crater throughput and can look like a stall. **Recommend the driver never expose /
  GETREG/SETREG-touch `0x1000–0x1FFF` in a hot path.** Correctness: fine. Performance: a foot-gun.

## Q9 — CLOSED: AutoConfig identity under `autoconfig_rtg YES`

**Recorded as closed: product byte `0x01` under `autoconfig_rtg YES`.**
Manufacturer `0x144B`, `er_Type` = Zorro III 128 MB. Corroborated (not re-derived):
emulated path `FW/Z3660_emu/src/cpu_emulator.cpp:338-349` (er_Product high nibble `0x0004`→0,
low nibble `0x0104`→1 ⇒ product `0x01`; `0x0100`→`0x3F..` = 128 MB Z3; `MANUF_ID 0x144B`,
`cpu_emulator.cpp:263`); documented real-CPU list `Z3660/docs/real-cpu-autoconfig-and-ext-rom.md`
(Z3 board mfg `0x144B`/5195, product 1, `$10000000`, 128 MB; FastRAM sub-board is product 2, the
Z2 SCSI board product 3). No disagreement.

## Q10 — Cache attributes expected from the 68k side

**Answer: firmware source dictates only the ARM-side policy (framebuffer Write-Through, register
window non-cached); it does not set the 68k-side attribute. Cache-inhibit on the 68k side — what the
port's kernel already does — is compatible and is the safe/required choice for the registers.**

- ARM-side (what the firmware controls): the register window (first 2 MB) is `NORM_NONCACHE`
  and the framebuffer is **Write-Through** — `RTG_FB_CACHE_POLICY_FOR_060 = 0x14DE2 | WT<<OUTER |
  WT<<INNER` (`FW/Z3660_emu/src/defines.h:9`; EMU variant identical, `:12`; soft3D is WB, `:10,13`).
  Applied at `main.cpp:945-948` (reg MB `0x180-0x182` NONCACHE, FB `0x182+` WT) and via
  `rtg_cache_policy_core0/1` (`FW/Z3660/src/main.c:1662-1665`, `cpu_emulator.cpp:398-459`).
- The **68k-side attribute is not set by firmware** and is not visible in this source — it is the
  driver/kernel's responsibility. From the mechanism:
  - **Registers MUST be serialized / cache-inhibited on the 68k side.** Writes are consumed through
    the `REG1` handshake (the FPGA must land the datum in DDR, then raise REG1, `rtg.c:559,583`), and
    reads must be fresh — a cached/reordered 68k access breaks the protocol. Use the same aligned
    32-bit + `nop`-sync-before-readback discipline as `amix-z3660scsi`/`amix-z3660net`.
  - **Framebuffer:** the ARM keeps it Write-Through, so it tolerates a 68k side that is either CI or
    write-through-with-flush; it does **not** tolerate 68k write-back without explicit flush. The
    proven, simplest choice is CI, which is exactly what the port's kernel does
    (`DTT0 = 0x003fc060` cache-inhibits the low 1 GB, per `amix-z3660scsi/src/z3660.c:688-709`).
- Verdict: your assumption "CI on the 68k side is required and is what the port's kernel does" is
  correct for the register window and safe for the framebuffer. Source can confirm the ARM policy and
  the handshake requirement; it cannot (and need not) mandate the 68k attribute.

---

## Register-map comparison — ZZ9000 (your `zz9000.h` / amiberry emu) vs Z3660 firmware

Cites: ZZ9000 = `DRV-zz9000.h` + `amiberry/src/zz9000.cpp`; Z3660 = `FW/Z3660/src/rtg/zzregs.h`
(+ handlers in `rtg.c`).

| Feature | ZZ9000 (classic 16-bit ABI) | Z3660 firmware (32-bit ABI) | Verdict |
|---|---|---|---|
| Register width | 16-bit (`DRV-zz9000.h`, `zz9000.cpp:180` `u16 registers[0x808]`) | 32-bit words (`zzregs.h`, `rtg.c:777,1050`) | **DIFFERS** |
| Register window size | `0x2000` (`DRV-zz9000.h:52`) | first **2 MB** `0x000000–0x1FFFFF` (`rtg.c:570-583`) | **DIFFERS** |
| Framebuffer offset | `0x10000` (`DRV-zz9000.h:53`) | `0x200000` (`memorymap.h:18`, `zzregs`/`init_rtg_bank`) | **DIFFERS** |
| HW_VERSION | `0x00` (`DRV-zz9000.h:19`) | none (read → `0xFFFFFFFF`, `rtg.c:1024-1026`) | **ABSENT on Z3660** |
| MODE | `0x02` (`:20`) | `0x100` `REG_ZZ_MODE` (`zzregs.h:28`) | offset DIFFERS; **encoding same** (mode&0xff, color (>>8)&0xf, scale (>>12)&0xf — `rtg.c:1111-1121`) |
| CONFIG | `0x04` (`:21`) | `0x104` (`zzregs.h:29`; int-enable/ack `rtg.c:1087-1109`) | offset DIFFERS |
| PAN | `0x0a/0x0c` HI/LO (`:22-23`) | `0x120` single 32-bit `REG_ZZ_PAN` (`zzregs.h:38`, `rtg.c:1055`) | **DIFFERS (one reg, not HI/LO)** |
| VCAP_MODE / capture | `0x0e`, CX `0x1000-0x1006` (`:24,28-31`) | `OP_CAPTUREMODE 0x30C` = **stub** (`rtg.c:1905-1908`); no VCAP | **ABSENT / stub** |
| VBLANK_STATUS | `0x4c` (`:25`) | `0x17C` (`zzregs.h:66`; read direct from FPGA, `rtg.c:840-841`) | offset DIFFERS |
| FW_VERSION | `0xc0` (`:26`), min `0x010d` (`:55`) | `0x1A0` (`zzregs.h:78`), returns `0x0103` (`rtg.c:805`) | offset DIFFERS |
| Blitter X1..SRC_PITCH | `0x10–0x32` 16-bit (`:33-49`) | `0x110–0x14C` 32-bit (`zzregs.h:33-51`) | **DIFFERS** |
| FILLRECT / COPYRECT | `0x22 / 0x24` (`:42-43`) | `0x134 / 0x138` (`zzregs.h:44-45`; `rtg.c:1239,1254`) | present; offset DIFFERS |
| DRAWLINE / P2C / P2D / INVERTRECT | (subset) | `0x158 / 0x154 / 0x15C / 0x170` (`rtg.c:1480-1533`) | Z3660 has full set |
| Hardware sprite | limited | SPRITE_X/Y `0x108/0x10C`, BITMAP `0x174`, COLORS `0x178` (`zzregs.h:30-31,64-65`; `rtg.c:1134-1180`) | present on Z3660 |
| SPLIT_POS | `0x5e` (`:50`) | `0x188` `REG_ZZ_SET_SPLIT_POS` (`zzregs.h:70`, `rtg.c:1540`) | offset DIFFERS |
| Overlay (YUV) | via CX/GFXData | `REG_ZZ_OVERLAY_* 0x320–0x34C` (`zzregs.h:197-208`; `rtg.c:2389+`) | present on Z3660 |
| Color modes | 8/565/32/15 = 0/1/2/3 (`:57-60`) | **same** (`FW/Z3660/src/rtg/gfx.h:105-110`) | **IDENTICAL** |
| Mode ids | 5=1080p; 17=1920x800 (`:62-81`) | 5=1080p60; 17=1280x800 (`zz_video_modes.h`) | overlap 0–11, **diverge ≥12** |
| Identity | mfg `0x6d6e`, product 3 (Z2)/4 (Z3) (`:11-16`) | mfg `0x144B`, product `1`, Z3 128 MB (`cpu_emulator.cpp:263,336,344`) | **DIFFERS** |

Net: color-mode ids and the MODE-word bit layout are the *only* directly reusable pieces. Everything
positional (offsets, access width, window/FB geometry, identity, mode-id table ≥12) must be retargeted.

---

## Assumptions the sheet could not verify from source (A1–A7)

- **A1** (= Q2c/d) — FB byte order + write-landing order on the FPGA path. **Partially answered:** the
  ARM/emulated model is big-endian-transparent with land-before-REG1 (`rtg.c:570,583`); the literal
  FPGA byte-lane wiring for the *direct* FB window is not in C. Confirm with a 16-bit bar fill on metal.
- **A2** (= Q6) — HDMI ownership during the demo. **Source part answered:** firmware owns the single
  HDMI output and there is no capture/native path (`video.c:245-270`, `rtg.c:1905-1908`). Which panel
  the operator watches is a metal observation.
- **A3** — where the combo board lands under `autoconfig_rtg YES` with the 256 MB RAM board also
  autoconfiguring. **Answered:** the RAM board takes `0x40000000` first, so the RTG combo lands at
  **`0x50000000`**; without the RAM board it lands at `0x40000000`. Cache-policy pre-map uses
  `ini = autoconfig_ram ? 0x500 : 0x400` (`FW/Z3660/src/main.c:1663-1664`, `:1487`), corroborated by
  `Z3660/docs/amix-firmware-facts.md:147-153` ("the `0x5000` row … with 256 MB of Z3 RAM configured
  first at `0x40000000`"). The exact address is the guest's AutoConfig assignment (`cpu_emulator.cpp:549`).
- **A4** — that a 32-bit read at `0x100001A0` on an AGA machine *without* a Z3660 bus-errors rather
  than returning open-bus. **UNSETTLED from source** — this is 68k/Zorro bus behavior, outside the
  firmware. Bus reasoning: an unclaimed Zorro III address on a real A3000/A4000 with no responding
  card typically bus-errors (no `/DTACK`/`/SLAVE`), which is why the compile-time gate is the right
  guard; but source cannot prove it. Keep the row AGA-gated. Note the firmware's *own* unknown-register
  path returns `0xFFFFFFFF` (`rtg.c:1024-1026`), so if a board *does* respond, do not treat
  `0xFFFFFFFF` as "present" — always test FW_VERSION `(v>>8)==1`.
- **A5** (= Q4) — 46 MB FB bound vs firmware clients. **Answered:** clear; firmware maps 126 MB, first
  client above the bound is GFXData at `+0x3200000` (`memorymap.h:21`).
- **A6** (= Q5) — demo monitor's actual accepted modes. **Metal read** — the timings exist and are
  60 Hz HDMI in source (`video.c:101-126`); monitor lock is not a source fact.
- **A7** — port question (segdev mmap of a `0x10000000` window). Not a firmware question; not addressed
  here.

---

## Emulation angle — can a Z3660 driver be built against amiberry's ZZ9000 emulation?

**Short answer: NO, not with a compile-time *identity* switch alone.** An identity switch only changes
the AutoConfig surface; it does not change the register ABI, and the two ABIs are incompatible.

- Amiberry's ZZ9000 emulation presents mfg **`0x6d6e`**, product **3** (Zorro II) / **4** (Zorro III)
  (`amiberry/src/gfxboard.cpp:446,453`) and implements the **classic ZZ9000 register ABI**: MODE
  `0x02`, CONFIG `0x04`, FW_VERSION `0xc0`, 16-bit `registers[0x808]`, "ZZ9000OS 2.7 register ABI"
  (`amiberry/src/zz9000.cpp:44-87,180`; `amiberry/docs/zz9000.md:3-6`). **This is exactly the ABI your
  `isoriano/zz9000-amix` driver targets** (`DRV-zz9000.h` MODE `0x02` / CONFIG `0x04` / FW `0xc0`).
  So the *existing* driver runs unchanged against the amiberry ZZ9000 emulation — good for validating
  the ZZ9000 path, useless for the Z3660 path.
- The Z3660 firmware ABI is the 32-bit `0x100`-based map (table above): different offsets, different
  access width, 2 MB register window, FB at `+0x200000`, identity `0x144B`/product 1. A driver written
  to those offsets will read/write the *wrong* cells of the amiberry ZZ9000 emulation.
- Therefore, to develop the Z3660 driver against emulation you need **one of**:
  1. an amiberry **Z3660-RTG emulation** that implements `zzregs.h` (the `0x100` map, `0x144B`/product 1,
     FB `+0x200000`) — the correct long-term path; or
  2. a **two-backend driver** with a compile-time (or probe-time) switch between the classic ZZ9000 ABI
     (for amiberry's current ZZ9000 emu / real ZZ9000 hardware) and the Z3660 ABI (for metal). This is
     a register-map abstraction, *not* just an identity `#define`.
- Bottom line for the porter: budget for a **register backend rewrite**, not an identity swap. The only
  free carry-overs are the color-mode ids and the MODE-word bit layout.

---

### Coverage summary
Q1–Q10 answered from source; Q9 recorded as closed. A1–A7: A1/A2/A6 answered in the part that is in
source with the metal residue named; A3/A5 answered; A4 unsettled-from-source (bus behavior, reasoning
given); A7 out of scope. The one genuinely unanswerable-from-source item is A4 (and the "no-Z3660 /
uninitialised" limb of Q3) — both are 68k-bus / FPGA-decode facts outside the C firmware.
