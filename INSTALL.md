# Installation

> **Fork note (amix-z3660rtg, 2026-09-26).** This file is upstream's ZZ9000 / A3000UX procedure,
> unchanged. For the Z3660 target (`-DZZ9K_TARGET_Z3660`, 68LC060 kernel), see `README.md`
> (fork section), `docs/DEMO-RTG.md` and `docs/X-INSTALL-RTG.md`.

These instructions assume a vanilla or locally maintained AMIX source tree at
`/usr/sys`. Existing local kernel changes should be preserved.

## 1. Install the driver source

From the repository root on AMIX:

```sh
sh install-source.sh
```

For a mounted or staged root, pass its prefix:

```sh
sh install-source.sh /mnt
```

The script only copies the self-contained ZZ9000 subtree. It does not rewrite
AMIX kernel files.

## 2. Integrate the kernel

Apply the small edits documented in:

- `integration/amiga-driver-Makefile.txt`
- `integration/kernel.c.txt`

Major `49` is used by the tested tree. Confirm that it is free in your own
`cdevsw[]` before adding the entry.

The optional displayed-VT console hook is documented in
`integration/c1-console.txt`. Omit it when the card is used only by X11.

## 3. Build and install the kernel

Build the driver first:

```sh
cd /usr/sys/amiga/driver/zz9000
make
```

Then build the full kernel using the normal AMIX configuration:

```sh
cd /usr/sys
make force
```

Install the kernel using your established AMIX procedure and reboot. Keep a
known-good rescue path available whenever replacing an AMIX kernel.

## 4. Create the device node

If `/dev/zz9000` does not already exist:

```sh
cd /usr/sys/amiga/driver/zz9000
sh mkdev.sh
```

The expected node is:

```text
crw-rw-rw-  root root  49, 0  /dev/zz9000
```

## 5. Build and install tools

```sh
cd /usr/sys/amiga/driver/zz9000
make install-tools
```

Run the hardware and framebuffer test:

```sh
/usr/amiga/bin/zz9k_test /dev/zz9000 16 1920x800
```

The report must show manufacturer `0x6d6e`, product `0x03`, a Zorro II board
address, firmware version, framebuffer geometry, and the requested mode.

## 6. Optional late console redirect

This requires the `c1.c` hook from `integration/c1-console.txt`.

```sh
cd /usr/sys/amiga/driver/zz9000
make install-rc
/etc/rc2.d/S99zz9000 start
```

Return HDMI output to the native RGB capture path with:

```sh
/etc/rc2.d/S99zz9000 stop
```

Do not add an early kernel-console takeover. The supported design activates
the RTG console only after normal AMIX startup has completed.

## X11 interface

X11R5 and X11R6.3 DDX drivers open `/dev/zz9000`, select a mode, query
`ZZ9KIOC_GETFBINFO`, and map the framebuffer. The public ioctl structures and
RGB masks are defined in `zz9000.h`.
