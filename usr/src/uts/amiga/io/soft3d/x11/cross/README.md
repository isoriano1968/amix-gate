# x11/cross/ — host-side inputs for the `Xzz9000` cross-build

Recipe: `docs/X-CROSS.md` (copy `host.def` to `src/xc/config/cf/host.def`, run imake with
`IMAKECPP=…/imakecpp.sh`).

- `host.def` — the cross-build overrides included by `site.def` after `amix.cf`.
- `imakecpp.sh` — the host preprocessor wrapper for imake.

These are scrubbed copies. The copies of record are the files the metal-proven binary was built
with, in the build dir `2026-09-18-xzz9000-cross/out/` (kept outside the repo):

| File | sha256 of the copy of record |
|------|------------------------------|
| `host.def` | `40542cb3fa63126e75dd8888e6b4b5fa4866338d7b40ec4c802df7bc1b290848` |
| `imakecpp.sh` | `a38c5f6771dcc1972287e3b564c32276856171338d87d01f710df462326570c9` |

## Substitutions (scrubbed copy vs copy of record)

`imakecpp.sh` is byte-identical (it holds no sysroot or home path; `/usr/bin/cpp` is the host cpp).

`host.def` gains one block of path parameters before the first override. Each is a cpp macro
with a default, overridable on the imake command line (`-DAmixSysroot=…`):

| Parameter | Default | Value in the copy of record |
|-----------|---------|-----------------------------|
| `AmixCrossPrefix` | `/opt/amix-cross` (the gcc-cross-amix / amix-cdfs convention) | (implicit in the sysroot) |
| `AmixSysroot` | `AmixCrossPrefix/m68k-cbm-sysv4/sysroot` | `<work>/sysroot-mirror` |
| `AmixXStubDir` | `$(TOP)/../../stub` (the work dir's `stub/`, from the `src/xc` layout) | `<work>/stub` |

`<work>` = the absolute path of the build dir (`2026-09-18-xzz9000-cross`, outside the repo) on the build host.

| Line | Copy of record | Scrubbed copy |
|------|----------------|---------------|
| `MathLibrary` | `-L<work>/sysroot-mirror/usr/ccs/lib -lm` | `-L AmixSysroot/usr/ccs/lib -lm` |
| `ServerExtraSysLibs` (1st `-L`) | `-L<work>/sysroot-mirror/usr/ccs/lib` | `-L AmixSysroot/usr/ccs/lib` |
| `ServerExtraSysLibs` (2nd `-L`) | `-L<work>/stub` | `-L AmixXStubDir` |

The space after `-L` is needed: cpp does not expand a macro name glued to `-L`. gcc and the
gcc-cross-amix wrapper both accept `-L dir`. With `-DAmixSysroot=<work>/sysroot-mirror
-DAmixXStubDir=<work>/stub`, `cpp -undef -traditional` expands both macros to the copy of record's
text, apart from that space.

The default sysroot is the plain toolchain sysroot. The recipe needs the mirror sysroot described
in `docs/X-CROSS.md` (the real sysroot plus `/usr/amiga`, `/usr/ucblib` and `libscreen.so`), so
point `AmixSysroot` at it.
