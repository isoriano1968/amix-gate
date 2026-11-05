#!/bin/sh
# configure-arm.sh -- FFmpeg 4.4.8's H.264 decoder and the sound decoders films
# come with (AAC, AC3/E-AC3, DTS, MP3/MP2, Vorbis: the float decoders, the player's format) for the Z3660's ARM
# (Zynq-7020 Cortex-A9 + NEON, bare metal, newlib), built in build-arm/,
# and the demuxers core 1 uses for streams it fetches itself (cores/amix/
# netsrc.c: MPEG-TS from amixcast.py, MP4/MKV/FLV/AVI from DLNA servers;
# its own HTTP, so no protocols and no network here):
#   PATH=~/opt/arm-none-eabi/bin:$PATH ./configure-arm.sh && make -C build-arm
# See docs/ARM-H264-PLAN.md.
set -e
V=$(cd "$(dirname "$0")" && pwd)
CPU="-mcpu=cortex-a9 -marm -mfpu=neon -mfloat-abi=hard"
mkdir -p "$V/build-arm"
cd "$V/build-arm"
"$V/ffmpeg-4.4.8/configure" \
  --enable-cross-compile --cross-prefix=arm-none-eabi- --target-os=none \
  --arch=arm --cpu=cortex-a9 --enable-neon --disable-runtime-cpudetect \
  --extra-cflags="$CPU -ffunction-sections -fdata-sections -Wno-error=incompatible-pointer-types" \
  --extra-ldflags="$CPU --specs=nosys.specs" \
  --disable-everything --enable-decoder=h264,aac,ac3,eac3,dca,mp3float,mp2float,vorbis \
  --enable-parser=h264,aac,ac3,dca,mpegaudio,vorbis \
  --enable-demuxer=mpegts,mov,matroska,flv,avi \
  --disable-pthreads --disable-w32threads --disable-os2threads \
  --disable-network --disable-programs --disable-doc --disable-avdevice \
  --disable-swresample --disable-swscale --disable-avfilter \
  --disable-postproc --disable-debug --disable-iconv --disable-zlib \
  --disable-bzlib --disable-lzma --disable-sdl2 --disable-xlib \
  --disable-vaapi --disable-vdpau
