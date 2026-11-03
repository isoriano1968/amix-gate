#!/bin/sh
# configure-openssl-arm.sh -- OpenSSL 3.5.4's libcrypto for the Z3660's ARM
# (Zynq-7020 Cortex-A9 + NEON, bare metal, newlib), for zzcrypto on core 1:
#   PATH=~/opt/arm-none-eabi/bin:$PATH ./configure-openssl-arm.sh && make -C openssl-3.5.4 build_libs
# The pristine archive in dl/ (the one amix-userland's library/openssl uses),
# unpacked as openssl-3.5.4/ and patched (patches/openssl-3.5.4-arm-bare-metal.patch:
# the CPU's features without an OS to ask); OpenSSL's own ARMv4/NEON assembler stays on.
set -e
V=$(cd "$(dirname "$0")" && pwd)
A=openssl-3.5.4.tar.gz
SHA=967311f84955316969bdb1d8d4b983718ef42338639c621ec4c34fddef355e99
CPU="-mcpu=cortex-a9 -marm -mfpu=neon -mfloat-abi=hard"
cd "$V"
[ -f dl/$A ] || curl -fL --retry 3 -o dl/$A https://github.com/openssl/openssl/releases/download/openssl-3.5.4/$A
echo "$SHA  dl/$A" | sha256sum -c -
rm -rf openssl-3.5.4
tar xzf dl/$A
cd openssl-3.5.4
patch -p1 < ../patches/openssl-3.5.4-arm-bare-metal.patch
# linux-armv4 for the assembler; only what zzc_arm.c's jobs need (EC, X25519,
# Ed25519, BN, SHA-2: the rest off, the image has to fit the reload window);
# no OS: no threads, sockets, DSO, file names, seeding (zzc_arm.c installs a RAND_METHOD fed by the 060's /dev/urandom)
./Configure linux-armv4 no-shared no-threads no-async no-sock no-dso no-engine no-tests \
	no-apps no-afalgeng no-ui-console no-dgram no-quic no-ktls no-secure-memory \
	no-autoload-config no-filenames no-legacy no-module no-docs --with-rand-seed=none \
	no-aria no-bf no-blake2 no-camellia no-cast no-chacha no-cmp no-cms no-ct no-des no-dh \
	no-dsa no-ec2m no-err no-gost no-http no-idea no-md4 no-mdc2 no-ml-dsa no-ml-kem no-slh-dsa \
	no-ocb no-ocsp no-poly1305 no-rc2 no-rc4 no-rc5 no-rmd160 no-scrypt no-argon2 no-seed no-siphash \
	no-siv no-sm2 no-sm3 no-sm4 no-srp no-srtp no-ts no-whirlpool no-ssl-trace no-sock no-psk \
	--cross-compile-prefix=arm-none-eabi- \
	$CPU -O2 -ffunction-sections -fdata-sections -fno-short-enums -DOPENSSL_NO_SECURE_MEMORY -DNO_SYSLOG -DOPENSSL_USE_USLEEP \
	-DOPENSSL_ARM_BARE_METAL_HWCAP=0x1000 \
	-D__ARM_MAX_ARCH__=7 -U__linux__ -Ulinux -I"$V/compat/arm"
