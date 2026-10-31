# Building the kernel side

Drivers, the 68040/68060 kernel port and the programs for the Z3660's core 1, on
a Linux PC. Start with
[getting started](https://github.com/isoriano1968/amix-userland/blob/main/doc/getting-started.md)
in amix-userland: it covers the sysroot, which is also needed here, and how
files get to the Amiga.

**This repository holds no AMIX source, headers or binaries, and none may be
added.** You build from your own AMIX installation.

## The compilers

| Code | Compiler | Installed in |
| ---- | -------- | ------------ |
| Kernel and drivers, `usr/src/uts` | GCC 2.7.2.3, `m68k-cbm-sysv4-gcc` | `~/opt/amix-cross` |
| Core 1 programs, `usr/src/firmware/core1` | `arm-none-eabi-gcc` | Your choice, on `PATH` |
| AMIX tools beside them, such as `zzglstat` | GCC 14, `m68k-elf-gcc` | `~/opt/m68k-amix` |

GCC 14 must never build kernel or driver objects: only GCC 2.7.2.3 links with
what AMIX itself was built from.

## 1. Build GCC 2.7.2.3

Its recipe is in amix-userland. `AMIX_ROOT` is your mounted or copied AMIX tree,
and needs `usr/include`, `usr/lib` and `usr/ccs/lib`:

```sh
cd ~/Sources/usr-amix/amix-userland/components/developer/gcc-27
make deps-hint
make all AMIX_ROOT=/path/to/your/amix
. build/env.sh
make validate-sysroot AMIX_ROOT=/path/to/your/amix
make test-hello AMIX_ROOT=/path/to/your/amix
```

It installs to `~/opt/amix-cross`. Add `PREFIX=<dir>` to choose another place.
Always go through the `m68k-cbm-sysv4-gcc` wrapper, because it repairs the
compiler's output. Calling the compiler binary directly gives silently wrong
code.

## 2. Get the repository

```sh
cd ~/Sources/usr-amix
git clone https://github.com/isoriano1968/amix-gate.git
```

## 3. The kernel

`usr/src/uts/amiga/port` patches the kernel binary from **your own** AMIX
installation. Its `BUILDING.md` is the guide, and needs more than the compiler:
a pinned NetBSD source archive, `m68k-linux-gnu` binutils and your AMIX kernel.
Start there, and run `tools/check-env.sh` first.

A wrong kernel can stop the machine from booting, so keep a known-good one, and
a way to boot it, before you try a new one.

## 4. Drivers

Each driver in `usr/src/uts/amiga/io/` has its own `README.md`, listing what it
needs and how it goes into the kernel. Most become part of a kernel built in the
step above. Check the device numbers in your own `kernel.c` before you add one.

| Driver | What |
| ------ | ---- |
| `io/paula` | Native Paula audio |
| `io/random` | `/dev/random`, `/dev/urandom` |
| `io/hydra` | Hydra Systems AmigaNet Ethernet |
| `io/z3660net` | Z3660 onboard Ethernet |
| `io/z3660scsi` | Z3660 SD storage as SCSI |
| `io/zz9000` | ZZ9000 and Z3660 RTG framebuffer, current version |
| `io/soft3d` | soft3D 3D |

## 5. Core 1 programs

These run on the Z3660's second ARM core. You need `arm-none-eabi-gcc` and a
checkout of the Z3660 firmware for its BSP, by default in
`~/Sources/usr-amix/upstream/Z3660-beta21`. `make Z3660=<dir>` picks another.

```sh
cd ~/Sources/usr-amix/amix-gate/usr/src/firmware/core1

# video: FFmpeg 4.4.8 unpacked or linked as ffmpeg-4.4.8/
sh configure-arm.sh && make -C build-arm
make -C cores/h264bench

# GL: Mesa 18.3.6 unpacked as src/mesa-18.3.6, patches/ applied with patch -p1
python3 -I build/xbuild.py arm
make -C cores/gpu
```

The result is `cores/<name>/build/z3660ARM_Core_<name>.bin`. Copy it to the
Amiga as described in the getting-started guide, into `/root/z3660ARMCores`, then
load it:

```sh
armbench -load z3660ARM_Core_h264bench.bin    # the first after power-on
armbench -reload z3660ARM_Core_gpu.bin        # afterwards
```

A program that hangs on core 1 needs a power cycle, because core 1 can't reload
while it is stuck.
