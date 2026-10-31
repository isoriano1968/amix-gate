#!/bin/sh
# configure-arm.sh -- FFmpeg 4.4.8's H.264 decoder for the Z3660's ARM
# (Zynq-7020 Cortex-A9 + NEON, bare metal, newlib), built in build-arm/:
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
  --disable-everything --enable-decoder=h264,aac --enable-parser=h264,aac \
  --disable-pthreads --disable-w32threads --disable-os2threads \
  --disable-network --disable-programs --disable-doc --disable-avdevice \
  --disable-avformat --disable-swresample --disable-swscale --disable-avfilter \
  --disable-postproc --disable-debug --disable-iconv --disable-zlib \
  --disable-bzlib --disable-lzma --disable-sdl2 --disable-xlib \
  --disable-vaapi --disable-vdpau
