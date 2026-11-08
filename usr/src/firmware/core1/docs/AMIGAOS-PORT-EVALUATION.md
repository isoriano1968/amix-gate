# Core 1 "amix" image under AmigaOS 3.2, and AmigaCast

Evaluation only (2026-11-07). Nothing was built, nothing was changed on the A3000.
Two questions:

1. Can the core 1 image `z3660ARM_Core_amix.bin` (GL + H.264 + crypto + net) be loaded
   and used from AmigaOS 3.2 on the A3000 + Z3660 with its real 68060?
2. What would an AmigaOS application "AmigaCast" take, doing what AMIX's AmixCast does?

Sources read: `cores/amix/{amix.c,Makefile,netsrc.c}`, `cores/h264bench/{bench.h,zzav.h}`,
`tools/armbench.c`, `README.md`; the firmware fork `upstream/Z3660-amixmode`
(`main.c` loader, `core1_net.h`, `AMIX-MODE.md`, `z3660-drivers/{rtg,ahi,mhi,common}`);
amix-userland `multimedia/ffmpeg/src/zzav.c`, `patches/0002-ffplay-amix-core1.patch`,
`desktop/cde-apps/amixcast/{amixcast.c,dlna.c,amixcast.py}`, `ztopcde/S98z3660core`.

## Short answer

- **The core 1 image runs unchanged under AmigaOS.** Core 1 knows only board offsets
  and its own ARM addresses; it never sees which OS runs on the 68k. The loader (C1LD in
  our firmware fork) is OS-independent too. Only the 68k client side has to be written
  for AmigaOS, and it is simpler than on AMIX: no `/dev/zz9000`, no mmap, no
  `ZZ9K_MAP_BOARD` kernel, no MMU tricks: `FindConfigDev(0x144B, 1)` and pointers.
- **AmigaCast needs no FFmpeg on the 68k.** With `ZZAV_CMD_OPEN_URL` the 060 only writes
  the URL and a few control words; core 1 fetches over its own lwIP, demuxes, decodes,
  shows the overlay and plays HDMI sound. The 68k side is a control client, a browser
  (DLNA + amixcast.py), and a GUI.
- **Needs**: our firmware fork (stock beta 21 has no loader), an Amiga cross toolchain
  (none is on the PC; a download is needed, see "Toolchain"), and test time on the box
  while it runs AmigaOS (i.e. AMIX not booted).
- **Recommendation**: do it, in three steps: (A) `c1load` + `zzplay` CLI (URL → core 1,
  window, keys) to prove the path, (B) the browser library (amixcast.py list + minimal
  DLNA client), (C) the ReAction GUI. Effort about 2–3 weeks of sessions in total, step A
  alone 3–4 days.

## 1. Core 1 under AmigaOS

### 1.1 Addresses: board offsets are the same, only the base differs

All of `zzav.h`/`bench.h` is written as offsets from the start of the Z3660's RTG board
memory (`board offset`). Under AmigaOS that is `cd_BoardAddr` of the RTG ConfigDev
(manufacturer 0x144B, product 1), the same thing `rtg/gfx.c`, `mhi`, `ZTop` use:

| what | board offset | AmigaOS address (RTG at 0x50000000) |
|---|---|---|
| ZZ registers (MPEG 0x2C0.., overlay 0x320..0x34C, audio 0x1EC/0x1F0) | 0x00000000 + reg | 0x50000000 + reg |
| P96 framebuffer / card memory (gfx.c: 0x200000 .. 0x3000000) | 0x00200000 | 0x50200000 |
| overlay buffer 0 / 1 | 0x06000000 / 0x06800000 | 0x56000000 / 0x56800000 |
| packet ring (unused with OPEN_URL), extradata | 0x07100000.. | 0x57100000.. |
| core 1 net rings (core1_net.h) | 0x07900000 | 0x57900000 |
| MPEG FIFO / shared block (overlay INIT) | 0x07A00000 / 0x07B00000 | 0x57A00000 / 0x57B00000 |
| ZZAV block, URL at +0x400 | 0x07AFD000 | 0x57AFD000 |
| C1LD loader block | 0x07AFE000 | 0x57AFE000 |
| HB_CTL (job/reload block) | 0x07AFF000 | 0x57AFF000 |
| HDMI sound ring | 0x07CE0000 | 0x57CE0000 |

The RTG base is 0x50000000 only with `autoconfig_ram YES` (0x40000000 + 256 MB); with
`autoconfig_ram NO` it is 0x40000000 (`AMIX-MODE.md`). So the program must take
`cd_BoardAddr`, never a constant. On AMIX the same offsets go through
`mmap(fd, off - 0x200000)` because `/dev/zz9000` offset 0 is board + 2 MB; that
subtraction disappears.

**P96 does not collide.** `gfx.c` gives Picasso96 0x200000..0x3000000 as card memory
(GFXData at 0x3200000). Everything core 1 uses is at 0x06000000 and above.

**Other AmigaOS users of the same area** (these do collide, see 1.5): `mhiz3660.library`
/ `axmp3` use 0x06000000–0x0702xxxx and the MPEG decoder regs; the AHI driver
`Z3660AX` uses the same HDMI sound ring 0x07CE0000 (`AUDIO_TX_BUFFER_ADDRESS`).

**Cacheability** (to check on the box, see open questions): the polled words (ZZAV block,
C1LD, HB_CTL, registers) must be read uncached by the 060. Under AmigaOS that depends on
how `68060.library` / MuLib map Zorro III space. If the RTG window is cacheable, either
mark the block pages cache-inhibited (mmu.library `SetProperties(MAPP_CACHEINHIBIT)`), or
`CacheClearE()` the page before every poll. Bulk copies (the 10.3 MB image) may go
copyback with a `CacheClearU()` after. On AMIX the kernel maps the board noncacheable
through `dev_kvmap`; AmigaOS gives no such guarantee by itself.

### 1.2 The image: unchanged

- `cores/amix/build/z3660ARM_Core_amix.bin` (11.3 MB now, `HB_IMAGE_MAX` 12 MB) runs at
  ARM 0x38000000, heap from 0x30000000 (`-DHB_HEAP_LOW`). Under AmigaOS the Z3 RAM at
  060 0x40000000 is ARM 0x20000000–0x2FFFFFFF, the same ARM range as under AMIX, so core
  1's memory is just as free.
- Words are big-endian, written by "the 68k": AmigaOS is the same 68k, no change.
- The GL server and zzcrypto parts are simply unused under AmigaOS (harmless). A later
  Warp3D/MiniGL/StormMesa bridge to zzgl would be a separate project.
- The name `amix` stays; an alias like `z3660ARM_Core_amigacast.bin` is not needed.

### 1.3 The loader: unchanged, a small AmigaOS client

Core 0's loader (`main.c` `core1_loader_poll`) takes `SRC` in board memory
(0x200000..0x7A00000), copies to `ADDR` 0x38000000..0x40000000, starts core 1 by
OCM 0xFFFFFFF0 + SEV. Rules that carry over 1:1:

- **once per power cycle** (`-4` afterwards); later images go by the harness's reload
  (`HB_CTL`, `armbench -reload`), only while core 1 is idle;
- **never** touch core 1 through the SLCR (froze the board);
- `-reload -force` is unreliable; a hung core 1 needs a power cycle (core1-recovery).

The AmigaOS client (`c1load`) is a port of the ~150 relevant lines of `armbench.c`
(`-load`, `-reload`, `-dump`): read the file into board memory at `HB_DATA_OFF`
0x06800000 (straight `Read()` into the RTG window, or via a fast RAM buffer and
`CopyMemQuick`), fill C1LD/HB_CTL, poll with `Delay()`/timer.device instead of `usleep`.
Startup-sequence or WBStartup entry: `c1load -auto SYS:Z3660/z3660ARM_Core_amix.bin`
doing what `S98z3660core` does (load if `loads == 0`, else reload, else leave a matching
running core alone).

**Requires our firmware fork** (`core1-loader`, or `amix-mode`, which contains it). Stock
beta 21 has no C1LD, no overlay fetch-width fix, no flip, no core 1 network. Since the
A3000 boots AmigaOS first with the same SD firmware before `pvmk`, the fork is already
what AmigaOS runs on this box.

**Across the AmigaOS ↔ AMIX boundary** core 1 survives warm resets and `pvmk`:
- loaded under AmigaOS, then AMIX booted: `S98z3660core` sees `loads=1` and reloads,
  fine **if** AmigaCast left the player idle (CLOSE, overlay off) before the reset;
- loaded under AMIX, then a warm reboot to AmigaOS: `c1load -auto` finds core 1 running
  and must reload or keep it, the same logic.

### 1.4 Video: the firmware overlay over a P96 screen

What zzav.c does on AMIX maps directly:

1. Overlay on through the firmware's MPEG INIT (with nothing to decode):
   `PARAM0..3 = board_base + SHARED_OFF, board_base, FIFO_SIZE, FIFO_OFF`, `CMD = INIT`.
   `board_base` is the 060 address, i.e. `cd_BoardAddr` under AmigaOS, which is exactly
   what core 0 expects in Amiga mode (it maps the RTG at its 060 address).
2. `ZZAV_W_OVL_PITCH = screen width * 4` (BGRA32, one row per display line), independent
   of the screen's depth (AMIX runs X at 16 bpp with the same overlay).
3. Place: `REG_OVL_XSTART/YSTART/WIDTH/HEIGHT` (HEIGHT last, it takes the new place),
   `ZZAV_W_OVL_ROW0`, shift/scale/maxw/maxh/DISPW as `geometry()`/`zzav_place()`.
4. Double buffer: `ZZAV_W_OVL_DBUF = 1` when `C1LD_W_FEAT & C1LD_FEAT_OVL_FLIP`;
   core 0 flips 0x06000000/0x06800000 at vblank. Same firmware, same behaviour.
5. 1080p: `vs = 1` when `C1LD_FEAT_OVL_FETCH`.
6. Close: `ZZAV_CMD_CLOSE`, then `MPEG CMD = CLOSE` (overlay off).

AmigaOS-specific points:

- **Screen geometry**: width/height from the window's screen (`GetBitMapAttr`/
  `p96GetBitMapAttr` or the display mode via `GetDisplayInfoData`); refuse or warn on a
  non-RTG (native chipset) screen, the overlay is only on the RTG's HDMI output.
- **Position**: an Intuition window as the video's frame; inner box =
  `win->LeftEdge + win->BorderLeft` etc., plus the screen's offset when it autoscrolls;
  move the overlay on `IDCMP_CHANGEWINDOW` / `IDCMP_NEWSIZE` (AMIX: `zzav_window()` on
  SDL window events). A borderless backdrop window on its own screen = fullscreen.
- **Occlusion**: the overlay is always on top. `REG_ZZ_OVERLAY_COLORKEY` (0x348) is in
  the register list but the firmware does not implement it. Same as on AMIX (X windows
  over the video are covered). Mitigation: hide the overlay when the window goes
  inactive or when its screen is not the front screen (poll `IntuitionBase->FirstScreen`
  in the player's tick, or screennotify.library if installed).
- **Screen switching** (Amiga-M, a game opening its own screen): the overlay would stay
  on top of the other screen → pause + overlay off when our screen leaves the front.
- **Do not use P96's own video window** (`CreateFeature(SFT_VIDEOWINDOW)` in gfx.c): it
  programs the same overlay registers with `OVERLAY_BASE` in P96 memory; two users of one
  overlay conflict. Our path talks to the registers directly, as on AMIX.

### 1.5 Audio: board HDMI directly (as on AMIX), AHI not in the path

Core 1 resamples to 48 kHz 16-bit stereo LE and writes the board's sound ring
(0x07CE0000, 8 × 20 ms), which the firmware's audio DMA plays round and round; the
player's clock follows the DMA (`ZZAV_W_CLOCKSRC` = sound). The 68k only starts the DMA
(`REG_AUDIO_PARAM = 0`, `REG_AUDIO_VAL = ZZAV_AUDIO_OFF`) and sets `ZZAV_W_VOLUME`.

Options under AmigaOS:

| | how | pro | contra |
|---|---|---|---|
| **HDMI direct (recommended)** | as AMIX | no 060 work, A/V sync already right, sound on the same HDMI as the picture | the same ring and DMA as the `Z3660AX` AHI driver: while a video plays, AHI sound from other programs is overwritten (and Z3660AX may have set another rate/scale) |
| AHI | core 1 writes PCM somewhere, a 68k task feeds AHI | mixes with other programs, any AHI device (Paula, Toccata…) | needs a new core 1 output mode (firmware/core change), 060 load, a new clock source, lip sync to redo |

So: HDMI direct, and AmigaCast must (a) restore what Z3660AX expects after CLOSE (rate,
`REG_ZZ_AUDIO_CONFIG/SCALE`: read the registers before, write back after; or simply
document "AHI's Z3660AX unit is unusable during playback") and (b) be told by the user
whether Z3660AX is the AHI device at all (open question). If the user's AHI goes to
Paula, there is no conflict.

### 1.6 Network: core 1's own, independent of Roadshow

`core1_net.h`: core 1 has a second (locally administered) MAC in the GEM's specific
address 2, its own RX ring and TX via core 0's main loop, DHCP by lwIP. The Amiga's own
path (`Z3660Net.device`, SANA-II → Roadshow) is unchanged ("beside the Amiga's"). So
under AmigaOS:

- streaming needs only the URL in `ZZAV_URL_OFF` + `ZZAV_W_START_MS` + `OPEN_URL`;
  the 060 and Roadshow carry no video bytes at all (AMIX's 145 KB/s limit does not apply);
- Roadshow is needed only for browsing (SSDP, SOAP, amixcast.py's `/list`), small data;
- AmixCast's `core1_fetches()` test (`ZZAV_FEAT_URL` and `C1NET_W_IP != 0`) carries over
  as is; the bitrate is then 2500 kbit/s without measuring.

## 2. AmigaCast

### 2.1 What AmixCast does today (AMIX)

- `amixcast.c` (Motif): a list shown as a tree ("+"/"-" folders, opened on demand), top
  entries = the PC's `amixcast.py` (plain HTTP: `/list`, `/stream?id&kbit&w&d&t`,
  `/info`, `/probe`) and every DLNA MediaServer found (`dlna.c`: libupnp + Pth, SSDP,
  description, ContentDirectory Browse in pages of 50, DIDL-Lite `res`).
- bitrate menu (auto/measured, or 2500 when core 1 fetches), `width_for(kbit)`;
- resume: `~/.amixcast/pos/<fnv1a(url or id)>`, asked when > 30 s in and > 30 s to go;
- play = `execl(/opt/amix/bin/ffplay -nostats -autoexit -window_title …)`; ffplay's
  patch hands the URL to core 1 (`zzav_url_open/play/seek/pause/volume`), keeps the window
  for the overlay, writes the position file on exit.

### 2.2 Feature map

| AmixCast | AmigaCast | effort |
|---|---|---|
| tree of folders, opened on demand | ReAction `listbrowser.gadget` with hierarchy (`LBNA_Generation`, `LBFLG_HASCHILDREN`, show/hide children), 1:1 | GUI |
| amixcast.py source (HTTP `/list`, `/stream`) | same protocol over bsdsocket, 1:1 | small |
| DLNA servers: search + Browse + `res` URL | minimal client (2.3), 1:1 in function | medium |
| config `~/.amixcast/config` (host port) | `ENVARC:AmigaCast/config` (+ ENV:), or tooltypes | small |
| resume positions `~/.amixcast/pos/<hash>` | `ENVARC:AmigaCast/pos/<hash>`, same hash; not shared with AMIX (separate disks) | small |
| resume question | `EasyRequest` / requester.class | small |
| bitrate menu, auto | cycle/chooser gadget, same rules | small |
| play / stop | in-process player task (no exec of ffplay) | part of zzplay |
| pause / seek (←/→ 10 s, ↑/↓ 1 min, click on a bar) | `ZZAV_W_PAUSE`; seek = OPEN_URL with the same URL + `START_MS` (core 1 only seeks) | 1:1 |
| volume / mute (ffplay 9/0, m) | `ZZAV_W_VOLUME` 0..256 | 1:1 |
| half size / scale 1–4 / fullscreen (ffplay keys) | same words; fullscreen = own RTG screen or backdrop window | 1:1 |
| position / length display | `CLOCK_MS - T0_MS`, `DUR_MS`, `BUF_MS`, `NET_KBS` | 1:1 (better than ffplay: a real status line) |
| stats on exit (`zzav_stats`) | same text into a log / `-v` | trivial |
| fallback to the 060 decoder for non-H.264 | **none** (no FFmpeg on AmigaOS side planned); use amixcast.py transcoding for such files | difference |
| stale-player cleanup on attach (1.5 s no movement → CLOSE) | same logic | small |

### 2.3 DLNA on AmigaOS: minimal client, not a libupnp port

libupnp needs pthreads, its own HTTP server thread, and was already a Pth adventure on
AMIX (patches 0004–0006, no multicast, blocking connect). A control point needs only:

1. **SSDP M-SEARCH** (`ST: urn:schemas-upnp-org:device:MediaServer:1`) by UDP to
   239.255.255.250:1900, and as on AMIX a broadcast fallback; collect `LOCATION:` for
   ~2 s with `WaitSelect()` timeout. Roadshow supports multicast send; with other stacks
   the broadcast fallback covers it.
2. **GET LOCATION** → `friendlyName`, the ContentDirectory service's `controlURL`
   (resolved against `URLBase`/LOCATION).
3. **SOAP POST Browse** (the envelope already in `amixcast.py` `BROWSE`), pages of 50,
   `TotalMatches`.
4. **DIDL-Lite**: `container id`/`item`, `dc:title`, `upnp:class`, `res` (+ `duration`,
   `protocolInfo`), entity-unescape the `Result`. A tiny tag scanner is enough (no
   expat), as Windows Media sharing quirks already handled in `dlna.c` show.

About 600–900 lines of C, synchronous, in the GUI's own task with timeouts (or a
browser subtask that posts messages to the GUI). Network API: `bsdsocket.library`
(Roadshow, AmiTCP, Miami all give it); nothing Roadshow-specific needed.

### 2.4 GUI toolkit: ReAction

| | for | against |
|---|---|---|
| **ReAction** (recommended) | part of AmigaOS 3.2 (classes on disk, headers in NDK 3.2), listbrowser with tree, layout/chooser/requester classes, font-sensitive | BOOPSI verbosity |
| MUI | nicest lists/tree (NListtree), many users | third-party (MUI 3.8 shareware / MUI 5), SDK + classes to install, a dependency not in 3.2 |
| GadTools / plain Intuition | no dependencies at all | no tree, no layout engine: own code for everything; fits only the video window |

The video window itself is plain Intuition either way (an empty window whose inner box
is the overlay, IDCMP for keys/moves).

### 2.5 Structure

```
amigacast/
  zzcore.c/.h   board via expansion.library, block pointers, cache handling,
                loader (load/reload/dump), overlay INIT/place/CLOSE, audio start/restore
  zzplay.c      URL → OPEN_URL → READY → overlay → OPEN; tick: pause/volume/seek/
                screen-front check/position file; Intuition video window + keys
  browse.c      amixcast.py /list + DLNA (ssdp.c, soap.c, didl.c) → node list
  gui.c         ReAction main window, tree, bitrate, resume requester
  c1load.c      CLI: -load/-reload/-dump/-auto (armbench for AmigaOS)
```

Two CLI tools fall out for free and are useful on their own: `c1load` and
`zzplay <url> [-ss secs]`.

## 3. Toolchain: nothing on the PC yet

Found on the PC: `~/opt/m68k-cross` (m68k-linux-gnu, ELF, Linux ABI), `~/opt/m68k-amix*`
and gcc14-cross-amix (AMIX/SVR4 ELF), `~/opt/arm-none-eabi` (core 1),
`~/AMIGA/Picasso96Develop` (P96 includes/fd/inline), `~/AMIGA/amiga/sysroot` (empty).
**No `m68k-amigaos-gcc`, no vbcc, no NDK 3.2, no bsdsocket/Roadshow SDK, no MUI SDK.**
None of the existing compilers emits AmigaOS hunk executables with the Amiga ABI (the
AMIX gcc 14 could in theory via an ELF→hunk converter, but without libnix/NDK it is not
a shortcut).

Options (each needs a download, i.e. your OK):

1. **AmigaPorts/bebbo `m68k-amigaos-gcc`** built into `~/opt/amiga` (gcc 6.5 or the 13.x
   branch, binutils, libnix, vasm, NDK 3.2 fetched by its Makefile). The Z3660 repo's
   `docker/Dockerfile` target `drivers-only` already does exactly this (~3 GB image,
   includes vbcc + amitools), but builds as root-in-docker; a native build in `~/opt`
   fits the user setup (no sudo) better. **Recommended**: same compiler family as our
   other work, `-m68060`, libnix (no ixemul), C99.
2. vbcc + NDK 3.2 (what the Z3660 driver Makefile uses). Smaller, fine for this program,
   but a second toolchain style.

Also needed: NDK 3.2 (ReAction headers, expansion, intuition), a bsdsocket header set
(NDK 3.2 has `proto/bsdsocket.h`? to check; else Roadshow's SDK or the AmiTCP SDK).

Testing without the box: the GUI and browsing run in FS-UAE (Kickstart 3.2 is there)
with its bsdsocket emulation; core 1 parts only on the A3000 under AmigaOS.

## 4. Effort

| step | content | estimate |
|---|---|---|
| 0 | toolchain + NDK 3.2 in `~/opt/amiga`, hello world in FS-UAE | 0.5–1 day |
| A1 | `zzcore` + `c1load` (load/reload/dump/auto) | 1–1.5 days |
| A2 | `zzplay`: OPEN_URL, overlay in a window, keys, seek/pause/volume, screen-front check, position files, audio restore | 2–3 days |
| A3 | box tests under AmigaOS (cacheability, overlay placement, AHI interplay, reload into AMIX) | 1–2 days of box time |
| B | browse: amixcast.py client + SSDP/SOAP/DIDL | 2–3 days |
| C | ReAction GUI, resume requester, config, icons, packaging (LhA, install script) | 2–3 days |
| | **total** | **~2–3 weeks of sessions**; step A alone ~4 days |

## 5. Open questions for the user

1. **Toolchain download**: OK to build AmigaPorts `m68k-amigaos-gcc` (+ NDK 3.2,
   libnix) into `~/opt/amiga`? Or vbcc? Or do you have one elsewhere (another PC, Docker
   image)?
2. **The AmigaOS side of the box**: which TCP stack (Roadshow? which version), is the AHI
   device the Z3660AX (HDMI) or Paula, which RTG screen mode/depth on Workbench, MUI
   installed at all? 68060.library / MuLib version (cacheability of the RTG window).
3. **Box time**: tests need the A3000 booted into AmigaOS, i.e. no AMIX (and none of the
   amix-box/ssh workflow) for those hours. When does that suit you? The amix-box lock
   cannot see AmigaOS; we would agree box time in chat.
4. **Where the code lives**: amix-userland is the AMIX distribution. An AmigaOS app could
   go to a new repo (e.g. `amigacast-amix`? the naming rule says -amix, but this is not
   AMIX) or into the Z3660 fork's `z3660-drivers/` beside ZTop. Preference?
5. **Audio**: accept "HDMI sound belongs to the video while it plays" (AHI's Z3660AX
   silenced), or is AHI output a requirement (then a core 1 change is needed)?
6. **Scope**: H.264 (+ AAC/AC3/EAC3/DTS/MP3/MP2/Vorbis) only, other codecs via
   amixcast.py transcoding — fine? GL/zzgl for AmigaOS (Warp3D/MiniGL bridge) is out of
   scope here; interested later?
7. **Autoload**: `c1load -auto` from the Startup-sequence/WBStartup on every AmigaOS boot,
   or only when AmigaCast starts? (Core 1 then stays loaded into AMIX, which
   `S98z3660core` already handles by reload, provided the player was closed.)

## 6. Risks

- RTG window cacheable under the user's 68060.library → stale polls; handled by MMU
  attributes or CacheClearE, must be verified first (step A3).
- MHI (`mhiz3660.library`, AmigaAMP etc.) and AmigaCast at the same time share
  0x06000000–0x0702xxxx and the MPEG regs: refuse to start while the MPEG decoder is
  busy (`REG_ZZ_MPEG_STATUS`) and document.
- An AmigaOS program crashing leaves the player running with the overlay on (no
  process cleanup like AMIX's atexit/kill path): the attach-time stale-player cleanup and
  a `c1load -close` (like `ovloff`) cover it.
- The loader's once-per-power-cycle rule: a core 1 hung by tests under AmigaOS blocks
  AMIX's core 1 use until a power cycle, as today.

## Status 2026-11-08

Toolchain built (AmigaPorts m68k-amigaos-gcc, GCC 16.2, libnix, NDK 3.2, in
`~/opt/amiga`). Steps A to C are written as a first version in
`~/Sources/usr-amix/amigacast` (uncommitted, provisional place): `c1load`, `zzplay`,
`castls`, `AmigaCast` (ReAction). Correction to 1.4: the firmware has no handler for
`REG_ZZ_OVERLAY_ENABLE` (0x320); zzplay hides the overlay with MPEG CLOSE and shows it
with INIT. Tested: browsing on Linux against the LAN and in FS-UAE (OS 3.2, bsdsocket
emulation), the GUI in FS-UAE. Not tested: anything with core 1 (needs the A3000 under
AmigaOS; steps in the amigacast README).
