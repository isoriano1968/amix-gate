# ZZ9000 AMIX Driver - Initial Zorro II Version

This subtree contains the first AMIX device-driver draft for the MNT ZZ9000.

AMIX does not support Zorro III, so this version deliberately probes only the
Zorro II product:

- manufacturer: `0x6d6e`
- product: `3`
- autocon id: `0x6d6e0003`

The driver exposes the Zorro II board window through `read`, `write`, and
`mmap`, plus a small native ioctl interface for board info, mode setting,
panning, RTG/capture switching, register access, and framebuffer fill.

## Hardware Model Used

From `BlitterStudio/zz9000-drivers`:

- registers start at board base
- framebuffer starts at board base `+ 0x10000`
- Zorro II usable framebuffer size is `board_size - 0x30000`
- mode register `0x02` is written as:

```c
mode | (color_mode << 8) | (scale_mode << 12)
```

This version does not include Zorro III, DMA, Ethernet, or USB.  It includes an
initial opt-in framebuffer console hook for output testing.

## Install Sketch

Copy `zz9000.c`, `zz9000.h`, and `Makefile` into:

```text
/usr/sys/amiga/driver/zz9000/
```

Then add `zz9000/exp` to the parent AMIX driver `Makefile`.

The suggested free character major is `49`.  In the current AMIX
`master.d/kernel.c`, `47` is Hydra, `48` is random, `50` is sad, and `49` is
unused.  Apply `kernel.c.patch` to wire `zz9000init` into `init_tbl[]` and add
the cdevsw slot:

```sh
cd /usr/sys
patch -p0 < /path/to/zz9000/kernel.c.patch
```

After rebuilding and booting the new kernel, create the device node:

```sh
mknod /dev/zz9000 c 49 0
chmod 666 /dev/zz9000
```

## Smoke Test

`tools/zz9k_test.c` is a small userland test program.  After the driver is
installed and `/dev/zz9000` exists, compile it on AMIX with the project header
available, then run:

```sh
zz9k_test /dev/zz9000
```

It calls `ZZ9KIOC_GETINFO`, sets a ZZ9000 mode, maps the Zorro II board window,
draws a flag test pattern at `fb_offset`, switches RTG output on, and restores
capture/native display when return is pressed.

Useful mode tests:

```sh
./zz9k_test /dev/zz9000 32 1280x720
./zz9k_test /dev/zz9000 16 1920x800
./zz9k_test /dev/zz9000 16 1600x1200
```

The first console hook is output-only and opt-in.  It switches the kernel
`conputc` pointer to a ZZ9000 chunky framebuffer renderer, uses AMIX's existing
25x12 Sun bitmap font, prints test text via kernel `printf`, waits for return,
and restores the original console hook:

```sh
./zz9k_test /dev/zz9000 console
```

Redirect mode keeps the normal AMIX keyboard/input path but redirects console
screen writes to the ZZ9000 framebuffer.  This is deliberately not a mirror:
the ZZ9000 can output either RTG or captured/de-interlaced native RGB, and the
last mode switch wins.

```sh
./zz9k_test /dev/zz9000 redirect
```

For normal login/shell output, apply the shared console renderer redirect hook:

```sh
cd /usr/sys
patch -p0 < ./amiga/driver/zz9000/c1.redirect.patch
```

If the older mirror hook was already applied to `amiga/console/c1.c`, convert
that hook instead:

```sh
cd /usr/sys
patch -p0 < ./amiga/driver/zz9000/c1.redirect.from-mirror.patch
```

## Late Init Console

The safe console handoff is a late userland step, similar in spirit to the
Hydra network configuration glue.  The kernel only probes the ZZ9000 at boot.
It does not switch the global console path while AMIX is still bringing up
filesystems, serial, STREAMS, and login services.

Build and install the userland helper:

```sh
cd /usr/sys/amiga/driver/zz9000/tools
make
make install
```

Then install the SysV startup script once you want the handoff to happen
automatically:

```sh
make install-rc
```

This installs `S99zz9000` into `/etc/rc2.d` by default.  If your AMIX
installation uses a different late run-level directory, copy
`/usr/sys/amiga/driver/zz9000/init/S99zz9000` there instead.  The script is
deliberately late and userland-driven: it expects the existing `/dev/zz9000`
device node, then runs `/usr/amiga/bin/zz9k_test /dev/zz9000 redirect`.

Manual control remains available:

```sh
/usr/amiga/bin/zz9k_console start
/usr/amiga/bin/zz9k_console stop
/usr/amiga/bin/zz9k_console test
```

The tools directory also includes a tiny ANSI-color directory lister for testing
the ZZ9000 console color parser:

```sh
/usr/amiga/bin/colorls -a /usr/amiga/bin
```

## Next Driver Work

1. Broaden ANSI handling and make late-init redirect robust enough for everyday
   login visibility.
2. Split public userland ioctls from private kernel definitions if the X11R5 RTG
   driver grows on top of this interface.
3. Keep network/USB separate until the framebuffer and console boot path are
   reliable.
