# FIRMWARE-RTG-INTERFACE.md — questions for the Z3660 firmware side

> **Out-of-repo citations.** Evidence directories are cited by their date-stamped names. They, sibling repositories and archived images are kept outside this repo and are not needed to build or use the driver.

**Purpose.** The amix-z3660rtg driver's Z3660 row (DESIGN.md §4.2) is built from a preliminary
read of the firmware and AmigaOS-driver sources (DESIGN.md §3). This sheet lists what still needs
the Z3660 firmware side's word — each question says *why it matters*, *where the answer lives*
(a firmware source line, or one read on the metal), and *what we currently assume*. Please
answer in place under **Answer:** (with a citation or the metal observation), leave the
question text alone, and mark anything you cannot settle as `UNSETTLED` with the reason. Your
own `2026-09-15-z3660-rtg/FIRMWARE-RTG-INTERFACE.md`, if you keep writing it, is the
long form; this file is the merge target the driver row is generated from.

**Merged 2026-09-15** from the Z3660 firmware side's answer sheet (verbatim copy:
`docs/FIRMWARE-ANSWERS-2026-09-15.md`; firmware branch `amix-main-2`, source-only). Status tags:
CLOSED = settled from source; METAL = only a metal read can finish it; UNSETTLED = outside the
firmware's C (bus/FPGA behaviour). The firmware side's `rtg.c` line numbers can differ from
DESIGN.md's by a few lines (same branch, a doc commit apart).

Paths: `FW/` = `Z3660/z3660-firmware/Z-TURN/vitis_ide/`, `DRV/` = `Z3660/z3660-drivers/`.
Every "metal read" below is a **read**; nothing here asks for a write on the demo machine,
and nothing is to be done on the metal before 2026-09-19. *(Rule of 2026-09-15; the METAL items were later closed on metal — see A1, A2, A6.)*

---

## Q1 — The hard-map at `0x10000000` under `autoconfig_rtg NO`

- **Need:** confirmation that the RTG/combo window is at guest `0x10000000` whenever
  `autoconfig_rtg NO`, in every Amix preset and both boot modes (`CPU` and the emulated
  ones), and that no other knob (`amix_mode`, `amix_ram`, `autoconfig_ram`, a preset overlay)
  moves or disables it.
- **Why:** the Z3660 row's `fixed_base`; the AGA-gated probe reads there without any AutoConfig
  evidence.
- **Where:** `FW/Z3660_emu/src/cpu_emulator.cpp:221-236` (`reset_autoconfig`), the real-CPU
  equivalent in the FPGA/firmware (`z3660.v` / whatever configures the bridge window for
  `bootmode CPU`), `config_file.c` for the knobs; on metal: the firmware's boot print
  `[Core1] Autoconfig RTG to 0x1000`.
- **We assume:** unconditional when `z3_enabled & 2 == 0`.
- **Answer:** **CLOSED.** The gate is exactly `autoconfig_rtg`: `shared->z3_enabled |= config.autoconfig_rtg ? 2 : 0`
  (`FW/Z3660/src/main.c:1694`). Emulated modes: `reset_autoconfig()` plants `0x10000000` and marks
  it configured (`cpu_emulator.cpp:221-226`). Real-CPU mode: `DISABLE_RTG_AUTOCONFIG` and the RTG
  window at MB index `0x100` (`main.c:1436,1440,1662-1665`). `amix_mode`/`amix_ram`/`autoconfig_ram`
  only shape the RAM window below; both map branches lay `dmmy_bank` over `0x10000000-0x17FFFFFF`
  which `init_rtg_bank()` overlays (`uae_emulator.cpp:1223-1225`), and the amix RAM window is clamped
  so it "can never run into the combo board window" (`:1180-1194`).

## Q2 — The real-CPU (68060) bus path for the window

- **Need:** (a) that every 68k access in the first 2 MB (`+0x000000..+0x1FFFFF`) is delivered
  to `rtg_thread()` and nothing in that range is decoded by the FPGA on its own; (b) that
  the framebuffer from `+0x200000` is a direct DDR window (no per-access ARM handling) for
  the real CPU; (c) the byte order the real CPU sees for 16-bit and 32-bit stores into the
  framebuffer (the emulated path `swap16/swap32`s; the FPGA path is not visible in C); (d)
  whether the FPGA has landed a register write in DDR before it raises `REG1`
  (`FW/Z3660/src/rtg/rtg.c:557`).
- **Why:** console and X performance (CPU fill loops of 16-bit pixels), and the correctness
  of every register write.
- **Where:** `z3660.v` (bus bridge), `rtg.c:552-720`; on metal: after a mode is set, a
  `zz9k_test`-style 16-bit bar fill visibly correct = (b)+(c) answered.
- **We assume:** (a) yes (`gpio & 0x1FFFFF`, `rtg.c:570`); (b) yes (`rtg_bank` from bank
  `0x1020`); (c) big-endian transparent; (d) yes.
- **Answer:** **CLOSED (a,b,d); (c) source model only, METAL residue = A1.** (a) `zaddr = gpio & 0x1FFFFF`
  (`rtg.c:570-571,644`) — the first 2 MB is forwarded to the ARM, nothing there is served by the
  FPGA alone. (b) the framebuffer from `+0x200000` is a direct DDR window: `rtg_bank` is
  `ABFLAG_RAM | ABFLAG_DIRECTACCESS`, 128 MB, `baseaddr = RTG_BASE` (`uae_emulator.cpp:972,984-990`),
  never seen by `rtg_thread()`. (c) big-endian transparent: the ARM path `swap32/swap16`s only to
  undo its own little-endianness (`rtg.c:570,583,602,655`). (d) the ARM reads the payload back out of
  DDR (`swap32(*address)`, `rtg.c:570,583`) — landed before `REG1` is raised.

## Q3 — The presence signature

- **Need:** the value of `REG_ZZ_FW_VERSION` (`0x1A0`) as a 32-bit read on the shipped
  firmware and on every firmware the demo might carry; whether `FW_BETA` (`0x280`) /
  `FW_ALFA` (`0x284`) are stable; what a 32-bit read at `+0x000` and at `+0x1A0` returns on
  a Z3660 whose RTG is *disabled* or not yet initialised (open-bus `0xFFFFFFFF`? zero? a
  bus error?).
- **Why:** `zz9k_present()` for the Z3660 row; the driver must never attach to an absent
  RTG and must never hang the boot.
- **Where:** `rtg.c:805-814`, `FW/Z3660/src/version.h`; the AmigaOS driver's own test is
  `fwrev >> 8 == 1` (`DRV/rtg/gfx.c:488-495`); on metal: one 32-bit peek at `0x100001A0`.
- **We assume:** `0x0103`, test `(v >> 8) == 1`.
- **Answer:** **CLOSED for a mapped RTG; UNSETTLED for the no-board/uninitialised limb (= A4).** `0x1A0` =
  `(REVISION_MAJOR<<8)|REVISION_MINOR` = `0x0103` (`rtg.c:805-807`, `version.h:1-2`); `FW_BETA 0x280`
  = 21 (stable), `FW_ALFA 0x284` = 0 in-tree, a build counter — informational only. `+0x000` reads
  `0xFFFFFFFF` (`rtg.c:1024-1026`) — **never probe `+0x000`**; probe `+0x1A0` and test
  `(v >> 8) == 1`. Under NO the bank is mapped at CPU reset (`cpu_emulator.cpp:117,137`), so it is
  live by guest boot.

## Q4 — The framebuffer envelope

- **Need:** that `[+0x200000, +0x3000000)` (46 MB) is free of every other client (piscsi
  mailbox `+0x2000`, boot ROM `+0x6000`, bounce `+0x80000`, GFXData `+0x3200000`, soft3D
  `+0x4200000`, audio `+0x7CE0000`, ethernet `+0x7E00000..`, USB `+0x7F00000`) in the Amix
  presets; whether the 46 MB bound is the firmware's or only the AmigaOS driver's choice
  (`DRV/rtg/gfx.c:511`); and whether the firmware itself ever writes into that range
  (e.g. its own boot splash / text console).
- **Why:** the row's `fb_phys` and `fb_max`, hence `board_size` and what `Xzz9000` mmaps.
- **Where:** `FW/Z3660/src/memorymap.h`, `rtg.c` (`video_state->framebuffer`), `video.c:143`.
- **We assume:** free; the bound is the AmigaOS driver's, safe to reuse.
- **Answer:** **CLOSED.** Firmware maps `[+0x200000, +0x8000000)` (`init_rtg_bank`, `uae_emulator.cpp:1023`);
  `[+0x200000, +0x3000000)` is clear of every client — boot ROM `+0x6000`, bounce `+0x80000` and the
  piscsi mailbox are below it in the register window; the first client above is GFXData at
  `+0x3200000` (`memorymap.h:21`), then soft3D `+0x4200000`, USB `+0x7F00000`. The audio/ethernet
  constants in `memorymap.h:28-34` are spelled as absolute low-DDR addresses on the ARM side; from
  the guest the net driver binds them as board-relative offsets in the top 32 MB of the window
  (`amix-z3660net/src/z3660eth.h:36-45`; ARM MMU reserves RTG-relative MB `0x60-0x80` for
  overlay/audio/mpeg/eth, `cpu_emulator.cpp:432-459`) — above the 46 MB bound either way. The 46 MB
  bound is the AmigaOS driver's, not the firmware's; safe to reuse. The firmware writes into the FB
  only for its own boot screen before the guest sets a mode (Q6).

## Q5 — Modes the demo monitor accepts

- **Need:** which mode ids the firmware outputs over HDMI at 60 Hz on the demo monitor
  (5 = 1920x1080, 17 = 1280x800, 18 = 1920x1200, 19 = 1600x900, 20 = 1680x1050, 21 =
  1366x768), and the preferred console mode.
- **Why:** the Z3660 row's `con_w/con_h` (default 1920x1080, a project decision) and the
  X server's mode list.
- **Where:** `FW/Z3660/src/rtg/zz_video_modes.h`, `video.c:100-140` (timing rows); on metal:
  whatever the Z3660's own AmigaOS/Picasso96 setup already shows on that monitor.
- **We assume:** 1920x1080 works.
- **Answer:** **CLOSED for the firmware side; monitor lock is METAL (= A6).** All listed ids are 60 Hz HDMI
  timings: 5 = 1920x1080 (`zz_video_modes.h:9`, `video.c:108`), 17 = 1280x800 (`:21`, `video.c:120`),
  18 = 1920x1200, 19 = 1600x900, 20 = 1680x1050, 21 = 1366x768 (`video.c:121-124`); each
  `video_mode_init` programs the pixel clock and the SiI9022 transmitter (`video.c:232-233`).
  Default 1920x1080 is sound.

## Q6 — Who owns the HDMI output

- **Need:** what the Z3660's HDMI shows before any RTG mode is set (nothing? the firmware's
  own console/splash? a scan-converted native Amiga screen?); whether that is the display
  the operator watches during the demo; and what happens to it when the driver writes
  `MODE` — is there any way back (the `OP_CAPTUREMODE` handler is a stub, `rtg.c:1905-1908`)?
- **Why:** the meaning of `SETSWITCH CAPTURE` / "noconsole" on metal, and demo safety.
- **Where:** `rtg.c` (`zz_set_monswitch`, video init), `video.c`; on metal: observation only.
- **We assume:** the switch is a no-op; RTG output is the only HDMI content.
- **Answer:** **CLOSED for the firmware side; which panel the operator watches is METAL (= A2).** The
  firmware owns the single HDMI output and drives it itself at boot: the `BOOT_SCREEN` block sets
  `config.bootscreen_resolution` in RGB565 and renders its own splash/console into the framebuffer
  (`video.c:245-270,698`). A guest `MODE` write reprograms the same output (`rtg.c:1111-1132`).
  `OP_CAPTUREMODE 0x30C` is a stub (`rtg.c:1905-1908`); `zz_set_monswitch` is never called;
  `REG_ZZ_MONITOR_SWITCH 0x318` is a preset field, not a live switch; `main.c:1651` calls
  `monitor_switch(1) // RTG` once. **There is no path to a native Amiga screen through the Z3660**,
  so `SETSWITCH CAPTURE`/"noconsole" cannot restore anything — a no-op by construction.

## Q7 — 16-bit register accesses from the real CPU

- **Need:** confirmation that a 16-bit CPU write into the register file is dispatched as the
  aligned 32-bit word containing it (`rtg.c:570-580` + `:1042`), and that a 16-bit read
  returns one lane (`:676-683`) — i.e. that the Z3660 row must use aligned 32-bit accesses
  only, exactly like `amix-z3660scsi`/`amix-z3660net`.
- **Why:** the accessor's `regw`; GETREG/SETREG width.
- **Where:** as cited; on metal: a 16-bit `SETREG` of `X2` followed by a 32-bit read would
  show it (not needed before 2026-09-19).
- **We assume:** yes — 32-bit only.
- **Answer:** **CLOSED.** For `zaddr < 0x1000` the real-CPU write path reads the full longword from the DDR
  staging cell and dispatches the whole register (`rtg.c:583`), no size decode; `write_rtg_register`
  / `read_rtg_register` mask to `zaddr & 0x1FFFFC` (`rtg.c:779,1042`) and log "write unaligned" on
  `zaddr & 3`; a read returns the full 32-bit register and the FPGA hands back the lane asked for.
  The emulated path is the same (`cpu_emulator.cpp:981-985`). **Aligned 32-bit accesses only.**

## Q8 — The `0x1000-0x1FFF` "test area"

- **Need:** that touching `+0x1000..+0x1FFF` is harmless (raw memory + a `printf` per access
  on the ARM UART, `rtg.c:573-577,646-673`) and that nothing else decodes there in any
  preset; whether the printf can be a problem at UART speed if userland pokes it in a loop.
- **Why:** `mmap` of the compacted device space hands page `0x1000` to userland; GETREG/SETREG
  allow it.
- **We assume:** harmless.
- **Answer:** **CLOSED — harmless for correctness, a hot-loop hazard.** `0x1000-0x1FFF` is raw DDR with an
  **unconditional** `printf` per access on the ARM UART (`rtg.c:573-577` write, `:646-673` read),
  not gated by `debug_console`; nothing else decodes there. A userland loop over that page blocks
  the RTG thread on UART writes. **Consequence adopted:** the Z3660 row's device space excludes the
  page (`fb_offset 0x1000`, `reg_size 0x1000`), so neither `mmap` nor GETREG/SETREG can reach it.

## Q9 — CLOSED (adversarial check): the AutoConfig identity under `autoconfig_rtg YES`

Product byte is `0x01`, manufacturer `0x144B`, `er_type` 128 MB Zorro III, on the emulated
path (`cpu_emulator.cpp:338-345`), the FPGA path (`z3660.v:973-996`) and the documented
real-CPU board list (`Z3660/docs/real-cpu-autoconfig-and-ext-rom.md:21,96-102`). Nothing to
answer unless the Z3660 firmware side disagrees.

## Q10 — Cache attributes expected from the 68k side

- **Need:** whether the firmware expects the framebuffer window to be cache-inhibited /
  serialized on the 68k side, or tolerates write-through; and whether the registers need
  serialized (in-order) stores (the siblings use a `nop` sync before read-back).
- **Why:** 060 metal correctness; `DTT0 = 0x003fc060` already cache-inhibits the low 1 GB
  on the port's kernel (`amix-z3660scsi/src/z3660.c:688-709`).
- **Where:** `FW/Z3660_emu/src/main.cpp:943-954`, `cpu_emulator.cpp:398-459` (ARM-side
  policies only).
- **We assume:** CI on the 68k side is required and is what the port's kernel does.
- **Answer:** **CLOSED.** The firmware sets only ARM-side policy: register window `NORM_NONCACHE`,
  framebuffer Write-Through (`defines.h:9,12`; `main.cpp:945-948`; `main.c:1662-1665`). The 68k-side
  attribute is the kernel's: registers must be cache-inhibited/serialized (the `REG1` handshake needs
  fresh, ordered accesses; use aligned 32-bit + a `nop` sync before read-back like the siblings); the
  framebuffer tolerates CI or write-through-with-flush, not write-back. CI on the low 1 GB
  (`DTT0 = 0x003fc060`) is correct and sufficient.

---

## Assumptions we could not verify from source (DESIGN.md §10) — as questions

- **A1** (= Q2c/d) — **CLOSED on metal (2026-09-16)**: 16-bit stores into the framebuffer through
  the kernel's copy loop produced the correct colours on the Z3660 HDMI and read back `f800f800`;
  the 16-bit *register* lane reads (`0000/0103`) stand on the emulator run (skipped in the
  kernel-copy mode).
- **A2** (= Q6) — **CLOSED (2026-09-16)** and seen on metal (the firmware splash
  "Firmware 1.03 BETA 21 … Z3 RTG AUTOCONFIG Disabled" before the first MODE write): the firmware owns the single RTG HDMI
  output; the native Amiga console is the RGB output, which is what the RGB capture sees;
  the RTG HDMI sits behind a hand-flipped HDMI switch. Enabling RTG cannot disturb the RGB
  console; a metal RTG test needs someone at the switch (DESIGN.md §6 step 4).
- **A3** — **CLOSED**: the 256 MB RAM board takes `0x40000000` first, so the combo lands at
  `0x50000000`; without it, `0x40000000` (`main.c:1663-1664,1487`; `docs/amix-firmware-facts.md:147-153`);
  the exact address is the guest's AutoConfig assignment (`cpu_emulator.cpp:549`).
- **A4** — **UNSETTLED from source** (68k/Zorro bus behaviour): an unclaimed Zorro III address on a
  real A3000/A4000 typically bus-errors (no `/SLAVE`), which is why the row stays compile-time
  gated. And because the firmware's own unknown-register path returns `0xFFFFFFFF`
  (`rtg.c:1024-1026`), `0xFFFFFFFF` must never count as "present" — only `FW_VERSION >> 8 == 1`.
- **A5** (= Q4) — **CLOSED**: clear; first client above the bound is GFXData at `+0x3200000`.
- **A6** (= Q5) — **CLOSED on metal (2026-09-16)**: the demo path's monitor/capture locked 1920x1080
  (reported 60 Hz) and 1280x800 (reported 56 Hz by the capture; the firmware says 60 Hz) from
  the Z3660.
- **A7** — port question, not the firmware's; open until the 040/060-port relink question is taken up
  after 2026-09-19.
