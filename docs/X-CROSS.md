# X-CROSS.md — cross-building isoriano's `Xzz9000` (X11R6.3) for the Z3660 RTG

Running log and recipe for the X lane (started 2026-09-17: X on RTG with olwm
etc., via the cross route). Everything here is measured unless marked *plan*. Work dir:
`2026-09-18-xzz9000-cross/` (`src/xc` = the prepared tree, `log/` = every command's
output, `imakecpp.sh` = the host preprocessor wrapper). Upstream is never edited: the
reference clone of https://github.com/isoriano1968/x11r6.3-amix at **cb61a21** (= upstream head,
confirmed 2026-09-18); every deviation lives in the build tree and is
listed under "Deviations" with the question it raises for isoriano.

> **Status (2026-09-26).** The binary built here ran on the bench (2026-09-18) and on metal. Initial working version, **metal-proven end to end**: the candidate-2 kernel (driver 7e5c0e8) + the cross-built `Xzz9000` brought the OpenLook desktop up on the Z3660 HDMI from a real A4000 (metal session 2, rows 0–7, `docs/DEMO-RTG.md`; status of record 2026-09-19). Performance is poor; it is the next lane. Whether ZZ9000 and Z3660 stay one driver is still open.
>
> **Build inputs.** `host.def` and `imakecpp.sh` are this recipe's two host-side inputs. They are
> tracked in `x11/cross/` as scrubbed copies (paths made parameters; `x11/cross/README.md` lists
> every substitution). The copies of record are in the build dir outside the repo
> `2026-09-18-xzz9000-cross/out/`, with their sha256 in `x11/cross/README.md`.
>
> **Out-of-repo citations.** Evidence directories are cited by their date-stamped names. They, sibling repositories and archived images are kept outside this repo and are not needed to build or use the driver.

## 0. Scope

Cross-build **only the `Xzz9000` server** — it is a static link of its own archives (`dix os
font mfb cfb cfb16 mi Xext xkb hw/amix hw/amix/rtg hw/amix/rtg/zz9000`) plus the system
libraries `-lsocket -lnsl -ldbm -lscreen` (+`-lm`) — and run it with what the box already
has: the stock X11R5 fonts, `rgb` database and clients (`olwm`, `xterm`, the user's
`.xinitrc`). No R6.3 shared libraries (`-fpic`, `ld -G -h`, `.sa` archives), no R6.3 fonts,
no target-run `rgb`/`bdftopcf`/`mkfontdir`: none of the toolchain's shared-library gaps
(gcc-cross-amix ROADMAP D12) are on this path.

## 1. Established (2026-09-18)

| Item | Fact | Evidence |
|---|---|---|
| Source tree | official X11R6.3 `xc-1/2/3.tar.gz`, sha256-verified by the repo's own `install.sh`, overlay cb61a21 applied; 153 MB | `log/00-install-source.log` |
| Host `imake` | builds on Linux from `config/imake/Makefile.ini` with `BOOTSTRAPCFLAGS="-DAMIX -DSVR4 -Dm68k"` (+`-std=gnu89 -fcommon` for a 1997 K&R source) | `log/01-imake-bootstrap.log` |
| Host cpp for imake | `imakecpp.sh` = `/usr/bin/cpp -undef` with the host imake's compiled-in `-Dlinux`/`-Dunix` dropped (imake forwards `-D` but not `-U`, and `Imake.cf` would select `linux.cf` beside `amix.cf`); run as `IMAKECPP=…/imakecpp.sh` | `log/top-Makefile.test`: `CC = gcc`, `STD_DEFINES = -DAMIX -Dm68k -DSVR4`, `CCOPTIONS = -ansi -finline-functions`, `EXTRA_LIBRARIES = -lsocket -lnsl`, `CPP = /usr/public/lib/gcc-cpp` — identical to the on-box configuration |
| The `libsocket` link gap (toolchain "known, open") | `select` is undefined and `libsocket.so` itself imports `syslog`/`seteuid`; `libc.so.1` exports only `_abi_select`; the plain names are **weak in `libc.a`** (`select(W) seteuid(W) syslog(W)`, `_select(T)`). GNU ld 2.8.1 does not pull archive members for a shared library's undefined symbols, so: **`-usyslog -useteuid` (joined form — the wrapper splits `-u NAME`) and `$sysroot/usr/ccs/lib/libc.a` after the shared libraries** → links clean, DT_NEEDED by name (`libsocket.so libnsl.so /usr/lib/libc.so.1`), `select`/`syslog`/`seteuid` bound statically from `libc.a` | `scratchpad/probe/seltest.c`, `seltest4` (49 312 B) |
| `-ldbm`, `-lm` | only as archives in `usr/ccs/lib` (not on the wrapper's default path): needs `-L$sysroot/usr/ccs/lib` | sysroot listing |
| `-lscreen` and `<amiga/screen.h>` | **the sysroot never copied `/usr/amiga`**: `usr/lib/libscreen.so` is a dangling symlink to `/usr/amiga/lib/libscreen.so`; the DDX includes `<amiga/screen.h>` from `/usr/amiga/include` (the port's `-I/usr/amiga/include`) — both must come from a running Amix (rig or image mount); asked of kerntools → `2026-09-18-usr-amiga/` | wrapper `resolve_lib` refuses `-lscreen`; header grep |
| Server mode table | `zz9000_modes[]` = 640x480 800x600 1024x768 1280x720 1280x1024 1920x800, default 1920x800 (`zz9000hw.c:13-23`); `-mode WxH` is the only selector (`amixIo.c:403-410`); the Z3660 row refuses 1920x800 and the table lacks 1920x1080/1280x800 | upstream source |
| Runtime on the box | `/dev/zz9000` (49,0); fonts via `-fp`; `rgb` via `-co`; `-ac -kb` (XKB device path unfinished upstream); keyboard/mouse through the screen manager (`OpenScreen`, `SIOCACTIVATE`, `SIOCSETINPUTMODE SIM_RAWKEY`) — Amiga side | `startXzz9000`, `rtgInit.c:164-198` |

## 1a. First build run (2026-09-18)

| Step | Result | Log |
|---|---|---|
| stub archive | `stub/libsockstub.a` = `select.o seteguid.o signal.o syslog.o time.o vfork.o` extracted from `libc.a` (closure computed against `libc.so.1`'s exports); the test links at 9 KB with `printf` etc. dynamic — replaces "whole `libc.a` after the shared libs" | this file §1, `stub/` |
| `site.def` | the after-vendor `#include <host.def>` uncommented (line 127; stock copy kept as `log/site.def.stock`) — the one edit to a stock file, build tree only | |
| `host.def` | `CcCmd/ServerCcCmd = m68k-cbm-sysv4-gcc`, `CppCmd/PreProcessCmd = m68k-cbm-sysv4-gcc -E`, `Ar/As/Ld` = the cross triple, `DependCmd` = host makedepend (never run), `MathLibrary = -L<sysroot>/usr/ccs/lib -lm`, `ServerExtraSysLibs = -usyslog -useteuid -lsocket -lnsl -L<sysroot>/usr/ccs/lib -ldbm -lscreen -L<X>/stub -lsockstub` | `src/xc/config/cf/host.def` |
| `make Makefile` / `make -f xmakefile Makefiles` / `includes` | clean; 3 s / 2 s; generated `CC = m68k-cbm-sysv4-gcc`, `STD_DEFINES = -DAMIX -Dm68k -DSVR4`, server `SYSLIBS` as intended | `log/02-04-*.log` |
| `lib/Xau lib/Xdmcp lib/xkbfile lib/font` | **built, 0 errors, 0 warnings** | `log/05-*.log` |
| `programs/Xserver` (`make -k Xzz9000`) | **437 compiles, 424 objects, 5 warnings, 0 errors outside the Amiga DDX**; archives `dix os mi mfb cfb cfb16 Xext xkb lbx record dbe XIE` built. **Every failure is `amix.h:38: amiga/screen.h: No such file`** (10 DDX files) — the pending `/usr/amiga/include` | `log/06-Xserver.log` |

**The first wall is not the toolchain**: it is the missing `/usr/amiga` tree (headers + `libscreen.so`),
which no available sysroot carries; asked of kerntools (rig) — an image mount is the
alternative. No question for isoriano yet.

## 1b. The server is built (2026-09-18)

| Step | Result | Log |
|---|---|---|
| `/usr/amiga` (kerntools, from the desktop golden image d1768e06…, 56 files sha-verified) | mirror sysroot `sysroot-mirror/` = per-entry symlinks to the real sysroot + `usr/include/amiga` + `usr/lib/libscreen.so` (5948 B: `OpenScreen CloseScreen DisplayScreen NewBitmap ScreenColors ScreenDeviceName ScreenError`); `AMIX_SYSROOT` points at it | |
| the ten DDX objects | compiled, `hw/amix`, `rtg`, `zz9000` archived | `log/07-Xserver-link.log` |
| `lib/lbxutil` | needs `g3states.h`, generated by **running** `mkg3states` — a build-time tool: built with the host gcc, the header generated, then the cross build proceeds (the second host tool after imake) | `log/08-lbxutil.log`, `log/mkg3states-host` |
| `lib/zlib` | built | `log/08-zlib.log` |
| `-ldbm` | the sysroot's `libdbm.a` is a symlink to `/usr/ucblib/libdbm.a`; `/usr/ucblib` pulled by kerntools (302 files sha-verified) and linked into the mirror's `ccs/lib`. In the final binary `dbminit`/`fetch` bind to **`libnsl.so`** at run time (it exports them), as the native link does | |
| the wrapper forwards `-ansi -finline-functions` to `ld` at link time (`ld: unrecognized -a option 'nsi'`) because imake puts `CCOPTIONS` into `LDOPTIONS` too | `host.def`: `DefaultCCOptions` empty, `StandardDefines OSDefines OSRelease -ansi -finline-functions` (compile-only). **Note for gcc-cross-amix:** the real driver drops compile-only options at link time; the wrapper passes unknown options to `ld` | |
| four more `libc.a`-only symbols at the final link | `gettimeofday` (DDX, os), `_fpsetround/_fpsetmask/_fpsetsticky` (needed by `libm.a`'s `rem.o`/`sqrt.o`) — same class as `select`; the stub archive is now `fpsetmask fpsetrnd fpsetsticky gettimeofday hrtsys select seteguid signal syscall syslog time vfork`.o (closure against `libc.so.1`) | `stub/` |
| **`Xzz9000` linked** | **1 919 891 B**, sha256 `709b8a97f5c0c073…` | `log/09-Xserver-link.log` |
| gates | `file`: ELF 32-bit MSB m68k 68020, dynamically linked, interpreter `/usr/lib/libc.so.1`; `objdump -p`: NEEDED `libsocket.so libnsl.so /usr/amiga/lib/libscreen.so /usr/lib/libc.so.1` (the screen library names itself by its box path); 133 dynamic imports, all libc/libsocket/libnsl/libscreen API; statically bound: `select syslog seteuid gettimeofday _fpsetround _vfork _syscall`; `T main InitInput InitOutput ZZ9000InitHW ZZ9000SetMode`; `scan060`: **124 hits** (64-bit `mulul`/`divsl` forms from `-O`) — **user mode, the LC060 kernel's ISP emulates them** (the port's `isp61_060.s`); the native build carries the same, since it is the same gcc 2.7.2.3 | `log/11-gates.txt`, `log/Xzz9000.lst` |
| deliverables | `out/Xzz9000`, `out/startx-rtg`, `out/host.def`, `out/imakecpp.sh` | |

No question for isoriano was needed to reach the binary. The wall-free path: host imake + host `mkg3states`, `host.def`, a mirror sysroot with `/usr/amiga` and `/usr/ucblib`, the stub archive for the shared libc's missing plain-name entry points, and the one-source-line mode patch.

## 2. Recipe (each step logged to `log/NN-*.log`)

1. `config/cf/host.def` (mine, in the build tree): `CcCmd`/`ServerCcCmd` = `m68k-cbm-sysv4-gcc`,
   `LdCmd`/`AsCmd`/`ArCmdBase` = the cross triple, `CppCmd`/`PreProcessCmd` = `m68k-cbm-sysv4-gcc -E`,
   `DependCmd` = the host `makedepend`, `ServerExtraSysLibs` = `-lsocket -lnsl -L<sysroot>/usr/ccs/lib -ldbm -lscreen`
   + `ExtraLoadFlags`/server link tail = `-usyslog -useteuid <sysroot>/usr/ccs/lib/libc.a`;
   `AMIX_SYSROOT` = a mirror sysroot with `usr/amiga/{include,lib}` added (as the port's Model-B
   mirror is made: symlinks to the real sysroot plus the additions).
2. `make Makefile` (host imake) → `make -f xmakefile Makefiles` → `make includes`.
3. `lib/Xau lib/Xdmcp lib/font lib/xkbfile` → `programs/Xserver` (`Xzz9000` only).
4. Gates on the binary: `file` (ELF m68k, dynamically linked, interpreter `/usr/lib/libc.so.1`),
   `objdump -p` NEEDED by name only, `nm -u` = the expected set, the port's `scan060.py` on the
   listing (the server runs in user mode — the ISP emulates the 64-bit forms — so this is
   informational), size.
5. Bench (kerntools' LC060 rig + stand-in + the candidate kernel; needs X11R5 on the image):
   `Xzz9000 :0 -mode 1024x768 -fp <R5 fonts> -co /usr/X/lib/rgb -ac -kb`, then `olwm` + `xterm`
   with `DISPLAY=:0`; frame captured.

## 3. Deviations from upstream (each a question for isoriano)

- *done (build tree only):* `x11/patches/zz9000hw-modes.patch`, `zz9000_modes[]` gains `1920x1080` and `1280x800` (the Z3660's demo modes); the
  default should not be a mode the attached board refuses — proposal: default to the first
  entry that the driver accepts, or make `-mode` mandatory. **Q-isoriano-1**: would upstream take
  the two entries (the ZZ9000 has 1920x1080 too) and a safer default?
- `host.def` and `imakecpp.sh` are build-host matters, not source changes; `g3states.h` is a
  generated file (host-built generator); the stub archive is link-host material.
- **Q-gcc-cross-amix-1** (not isoriano): the wrapper forwards compile-only options (`-ansi`,
  `-finline-functions`) to `ld` at link time; the real gcc driver drops them. Worked around in
  `host.def`; a wrapper fix would make plain imake-generated link lines work.

## 4. `startx-rtg` design (*plan of 2026-09-18*)

*Built as `x11/startx-rtg` and metal-proven. The shipped launcher differs from this plan in session-file choice and readiness detection; see `docs/X-INSTALL-RTG.md` §2–§3.*

`rtg-mode <WxH>` validates the name against the driver's table for the attached board (via
`zz9k_mode`, no drawing) and the server's table, then `exec Xzz9000 :0 -mode WxH …` in the
background; wait for `/tmp/.X11-unix/X0`; run the user's `~/.xinitrc` (`olwm` etc.) with
`DISPLAY=:0`; on exit kill the server. Default mode 1024x768, overridable; the
server does its own `SETMODE` — `rtg-mode` only chooses. Keyboard/mouse: the DDX's
screen-manager path (Amiga side); nothing to configure.
