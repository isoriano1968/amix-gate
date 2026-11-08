# Core 1 programs: versions and what they offer

Every core 1 program (`z3660ARM_Core_<name>.bin`) puts its name, version and
parts into the control block (`bench.h`: `W_NAME`, `W_VERSION`, `W_PARTS`).
`ztop` and `armbench -dump` show them, e.g.

    ARM core 1      amix, idle, load 0.1 %, 866 MHz, v1.4 [gl h264 crypto net]

(The version comes last: launchers look for "amix," right after the name.)

A core from before versions shows no version and no parts.

Parts (`HB_PART_*`):

| part | what it is |
|---|---|
| gl | zzgl, the GL server: Mesa 18.3 softpipe (GLQuake, ioquake3, RtCW, fsv, gears) |
| h264 | the zzav player: H.264 decoding, sound (AAC, AC3, E-AC3, DTS, MP3, MP2, Vorbis) to HDMI |
| crypto | zzcrypto: public-key jobs for OpenSSL (ssh, curl) |
| net | core 1's own network (lwIP): ffplay/vplay hand it http:// URLs |
| glbench | the Mesa benchmark (osbench) |
| netbench | the lwIP benchmark |
| uae | the Amiga emulator: uae4arm, built in amix-userland emulators/uae4arm (`z3660ARM_Core_uae.bin`) |

When a program changes what it offers or how it behaves, raise its version
here and in its source (`hb_core_version`).

## amix — gl, h264, crypto, net

| version | date | commit | change |
|---|---|---|---|
| 1.4 | 2026-11-06 | b49d80f + versions | an even 2,3 cadence, faster pictures, a filtered stretch; reports its version and parts |
| 1.3 | 2026-11-05 | 70660b7 | VLC-like frame timing; AC3, E-AC3, DTS, MP3, MP2 and Vorbis sound |
| 1.2 | 2026-11-05 | 6aa7871 | fetches streams itself (OPEN_URL: lwIP + libavformat) |
| 1.1 | 2026-11-03 | 85b7634 | zzcrypto: public-key jobs for OpenSSL |
| 1.0 | 2026-11-03 | d7cca89 | gpu and the H.264 player in one image, 242 MiB heap |

Versions 1.0 to 1.3 were named afterwards; those images report no version.

## gpu — gl

| version | date | change |
|---|---|---|
| 1.0 | 2026-11-06 | first with a version: the GL server alone, 245 MiB heap |

## h264bench — h264

| version | date | change |
|---|---|---|
| 1.0 | 2026-11-06 | first with a version: the H.264 player and benchmark alone |

## glbench — glbench

| version | date | change |
|---|---|---|
| 1.0 | 2026-11-06 | first with a version |

## netbench — netbench

| version | date | change |
|---|---|---|
| 1.0 | 2026-11-06 | first with a version |

## uae — uae

Built in amix-userland `components/emulators/uae4arm` (its own harness in
`files/core1/core1_main.c`, this directory's `bench.h`), protocol in
`files/core1/c1uae_board.h`, run by `c1uae`.

| version | date | commit | change |
|---|---|---|---|
| 0.1 | 2026-11-08 | (amix-userland) | first image: interpreter, files in memory, overlay, HDMI sound, input ring, CoreMark bench |
