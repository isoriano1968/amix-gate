# z3660rtg: the Z3660 accelerator's RTG for AMIX

`/dev/z3660rtg`, character major **55**, minor 0: the framebuffer, mode and
blitter driver for the RTG of the Z3660 (68060 + ARM accelerator for the
A3000/A4000), as its own driver beside the MNT ZZ9000's (`../zz9000`,
`/dev/zz9000`, major 49). A machine with both boards has both drivers, each
with its own node, and each says nothing at boot without its board.

## Where it comes from, and the split

Until now one source, `../zz9000/overlay/usr/sys/amiga/driver/zz9000/zz9000.c`,
served both boards: the ZZ9000 rows by default, the Z3660 rows with
`-DZZ9K_TARGET_Z3660`, always as `zz9000*` on major 49. That source is the
Z3660 fork of the ZZ9000 driver, Jussi Alanärä's **amix-z3660rtg**
(github.com/jusii/amix-z3660rtg, MIT), plus our work on it:

| | amix-z3660rtg 1.0 (`91667de`) | `../zz9000` (amix-gate) |
|---|---|---|
| `zz9000.c`, `zz9000.h` | the Z3660 target behind `-DZZ9K_TARGET_Z3660` | + `dev_kvmap` above VSECT1, console naming the board, console query, soft3D board mapping, VT hooks (our branch `z3660-amix-a3000`), mode table with refresh rates, silent without a board, colour modes per board and `RTGIOC_GETCAPS` |
| tools (`zz9k_test`, `zz9k_mode`, `ztop`) | yes | yes, newer, + `zz9k_modes` |
| `DESIGN.md`, `NOTES.md`, `docs/` | design, firmware interface, bench and metal records | no |
| `x11/` (Xzz9000 patches, startx-rtg, kbdback, bench) | yes | no: the X server is amix-userland's `x11/xserver-amix` |
| `bench/zzpanfix.c` | yes | no |

The split, as decided with the user ("z3660 should have its own name ...
people may have more than 1 graphics card"):

- **The source here is the upstream, pristine, plus patches** (oi-userland
  style, as `../z3660scsi` and `../z3660net`): `src/zz9000.c` and
  `src/zz9000.h` are amix-z3660rtg 1.0 (`91667de`) as published, pinned by
  sha256 in `port/make-kit-040.sh`; `port/patches/` takes them to the driver:

  | patch | from |
  |---|---|
  | 0001-0005 | our amix-z3660rtg branch `z3660-amix-a3000` (`b4b1845`..`ccebccb`) |
  | 0006-0007 | amix-gate `32e04d7`, `02e4e48` (io/zz9000) |
  | 0008 | colour modes per board row and `RTGIOC_GETCAPS` (amix-gate `4b2586f`, io/zz9000) |
  | 0009 | **z3660rtg**: the Z3660 rows only, its own names, the fixed window `bprobe()`d and silent |

- **The two drivers share every fix.** After 0008 the build compares the
  result with `../zz9000`'s `zz9000.c` and `zz9000.h` and stops if they differ:
  a change made to `../zz9000` alone must be carried here as a new patch
  before 0009 (and `DRIFT_LAST` moved), or the z3660rtg kit does not build.
- **What 0009 changes:** `ZZ9K_TARGET_Z3660` is implied; the ZZ9000 rows and
  their register/mode tables are gone, so the driver binds only the Z3660:
  AutoConfig `0x144B0001` (manufacturer 0x144B, product 1, as `z3660scsi` and
  `z3660net` use), or its fixed window `0x10000000` on an AGA machine whose
  AutoConfig table does not list it. That fixed window is now read once
  through `bprobe()` first (an A4000 without a Z3660 has empty Zorro III space
  there, a bus error) and is silent when nothing answers. Every global has
  its own name (`z3660rtginit/open/close/read/write/ioctl/mmap`,
  `z3660rtgconsoleputc`, `z3660rtgvtinfo/vtpan/vtdirty`,
  `z3660rtg_blit_on/_blit_maxarea`) and the console lines say `z3660rtg:`.
- **The ioctl ABI stays `zz9000.h`'s**, unchanged, so `zz9k_test`,
  `zz9k_mode`, `ztop` and the X server's zz9000 card code drive
  `/dev/z3660rtg` once they take the device name (see "Next" below).
- **Docs:** `docs/upstream/` holds amix-z3660rtg 1.0's `DESIGN.md`,
  `NOTES.md`, `CHANGELOG.md` and `docs/*.md` unchanged (the firmware RTG
  interface, metal probe and bench records); `LICENSE` and `NOTICE` are
  upstream's. Not imported here: `x11/` and `bench/`, which belong to the X
  server component in amix-userland.

## Colour modes and capabilities

The Z3660's HDMI is a 16-pin bus, RGB565: a 32-bit mode shows nothing
(tested 2026-11-07). Its firmware takes 8-bit (CLUT) and 15-bit modes as
well (Z3660 `src/video.c` `video_mode_init`: 1 and 2 bytes per pixel through
the same formatter). Every board row now lists the colour modes its board can
show (`ZZ9K_CM_Z3660` = 8, 15, 16 bit; `ZZ9K_CM_ZZ9000` = all four), and
`ZZ9KIOC_SETMODE`/`ZZ9KIOC_SETMODEHZ` return `EINVAL` for any other.

`RTGIOC_GETCAPS` (`../rtgcaps.h`, plan row 0.8; the same block is in
`zz9000.h` byte for byte, the kit scripts compare them) answers the board
name, the depths it can show (`RTG_DEPTH(8) | RTG_DEPTH(15) | RTG_DEPTH(16)`
on the Z3660, plus `RTG_DEPTH(24)` on the ZZ9000), the framebuffer size, the
largest mode, and `RTGCAP_*`: console redirect, fill/copy by ioctl, card
blitter, mode list, refresh selection (rows with known rates: the Z3660),
soft3D (`-DZZ9K_MAP_BOARD` kernels with a 32-bit register row: the Z3660),
monitor switch (a board with a capture path, or a `-DZZ9K_MONSW` kernel).
`ENXIO` without a board.

## AMIX Installer kit (68040/68060)

`port/make-kit-040.sh` (with `AMIX_ROOT` set to an AMIX 2.1c tree with
`usr/sys/amiga/console`, `m68k-cbm-sysv4-gcc` on PATH) builds `port/kit-040`
(gitignored): `z3660rtg_040.o` (gcc 2.7.2.3, `-m68040`, Model-B headers,
`-DZZ9K_KVMAP -DZZ9K_FB_NC -DZZ9K_MAP_BOARD -DZZ9K_NO_BUILTINFONT`: the z3660
variant's flags without its Commodore console font), `devkvmap040.o`
(`provides=dev_kvmap dev_kvunmap`, shared with va2000-040 and rtgfb-040),
the pad, `patch_z3660rtg_cdevsw.py` (cdevsw[55], all six fields asserted
`nodev`), `patch_io_init.py`, `check_relink_relocs.py`, `KIT.info`
(`id=z3660rtg major=55 node=/dev/z3660rtg line=040 init=z3660rtginit
conflicts=zz9k_regoff_z3660`) and `KIT.sha256`.

Major 55 is `nodev` in stock AMIX 2.1c, the generic port kernel and the
z3660 variant (checked 2026-11-17; taken: 46 audio, 47 Hydra, 48 random,
49 zz9000, 50 sad, 51 zen0, 52 zz9000eth, 53 paula, 54 rtgfb, 56 xsurf100,
67 Xsvga, 68 VA2000).

There is no 68030 kit: the Z3660 is a 68060 card, and the stock 68030
kernel never runs on it.

## The z3660 kernel variant keeps its built-in RTG, for now

`tools/amix-kernel-040 --variant z3660` (the A3000 + Z3660 box line) still
builds `../zz9000`'s driver in with the Z3660 rows, on major 49 as
`/dev/zz9000`, as the box runs it. So:

- **`kit-relink.sh` refuses z3660rtg-040 on that base**: `KIT.info`
  `conflicts=zz9k_regoff_z3660` names the built-in driver's Z3660 register
  row, which that kernel has (a local symbol: the check reads every
  binding). It also refuses zz9000-040 there (the base has `zz9000init`).
- **The AMIX Installer must not offer the z3660rtg kit for that kernel**:
  with the "Z3660 optimised" kernel the RTG is built in; with a generic
  68040/68060 kernel and a Z3660 it links this kit.
- Moving the variant to this driver (major 55, `/dev/z3660rtg`) is a later,
  separate step: it changes the box's device node and needs the X server,
  `startx-z3`, the zz9k tools and `S99zz9000` to follow.

Byte identity: the colour-mode change (patch 0008) is in `../zz9000` too, so
the z3660 variant built from amix-gate after 2026-11-17 is no longer the
box's kernel byte for byte (the driver row table and SETMODE check change,
and with them the build id). This is deliberate: guarding it off for the
variant would keep the 32-bit bug there.

## Next

- X server (amix-userland `x11/xserver-amix`): the zz9000 card code opens
  `/dev/z3660rtg` first, then `/dev/zz9000` (or takes `-device`), and asks
  `RTGIOC_GETCAPS` for the depths it offers; the build takes the `x11/`
  patches from a pinned amix-z3660rtg in amix-userland, not from the
  upstream checkout.
- Tools: `zz9k_test`, `zz9k_mode`, `zz9k_modes`, `ztop` and `S99zz9000` take
  the device (`-d /dev/z3660rtg`), and `S99zz9000` redirects the console only
  when `RTGCAP_CONSOLE_REDIRECT` is set.
- AMIX Installer: link z3660rtg-040 for "Z3660 RTG" with a generic 68040/060
  kernel, never with the z3660 variant; mknod `/dev/z3660rtg c 55 0`; X card
  `z3660` (netinstall.py's `X_CARD`).

## Licence

MIT (`LICENSE`, upstream's); `NOTICE` is amix-z3660rtg's. The patches are
under the same licence.
