# Changelog

## Unreleased - 2026-11-07 (Display panel pass 2)

- `ZZ9KIOC_GETMODES` (19): the mode table with refresh (1/100 Hz) and pixel
  clock (kHz) per firmware timing; read-only.
- `ZZ9KIOC_SETMODEHZ` (20): set the size's timing nearest a refresh rate.
  Z3660: a second 1920x1080 row, firmware id 7 at 50.5 Hz; plain
  `ZZ9KIOC_SETMODE` still picks id 5 (60.6 Hz).
- The VT console buffer grows to 8 MB when a mode needs it (1920x1200,
  2560x1440), ending at framebuffer offset 0x2400000, clear of soft3D's
  data block.
- Tools: `zz9k_modes` (lists the table), `zz9k_mode WxH@HZ`.
- Tested 2026-11-07 on the A3000 in /stand/unix (68060-1107-1645, 272 MB):
  18 modes listed; 1920x1080@50 switches to id 7 (monitor shows 50 Hz,
  X unchanged) and back to id 5 with @60.  Known: the 8 MB console buffer
  is not mapped ("no kernel space for a 0x800000 console buffer", the
  kernel map window is full), so the text consoles keep 4 MB.

## 0.1.0 - 2026-07-03

- Initial public AMIX release.
- Zorro II product 3 discovery and attachment.
- Mode setting and RGB565/32-bit framebuffer mapping.
- Framebuffer information, fill, copy, pan, and display-switch ioctls.
- Color framebuffer console with late SysV init activation.
- Diagnostic, mode-test, console-control, and colorls tools.
- Interface used by the ZZ9000 X11R5 and X11R6.3 RTG servers.
