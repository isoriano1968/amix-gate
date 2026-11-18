# a4091: the A4091, A4092 and A4770 SCSI for AMIX

The NCR 53C710 Zorro III SCSI-2 boards -- Commodore's **A4091** (AutoConfig
0x0202/0x54) and the **A4092** and **A4770** that reuse its design (SCSI.ME,
0xc0de/0x01 and 0xc0de/0x02) -- as controllers of AMIX's own SCSI disk layer:
their drives are ordinary dd disks, block major 18 / character major 40,
`/dev/dsk/cNdUsS` and `/dev/rdsk/cNdUsS`, the unit being the SCSI ID.  On an
A3000 the onboard SCSI is card 0 and the A4091 card 1, so a core disk at ID 6
on the A4091 roots on `ced0s1`; on an A4000 the A4091 is card 0 (`c6d0s1`).

## Where it comes from

`src/a4091-wr.c` and `src/a4091_dmarec.h` are the static A4091 driver of
amigaux's AMIX (Jussi Alanärä, MIT, `LICENSE`) at the commit `UPSTREAM`
names, and `tools/a4091-widths.py` its register width check; all unchanged and
pinned by sha256 in `port/pins.sh`.  Upstream links the driver into kernels its
own build hub generates, with an `sdcardbase()` added to sd.c, or loads it as
an amigaux 1.1 module.  Upstream's patches to AMIX's own support.c, scsi.c,
a3091.c and a2091.c are not part of this kit.

## The kit

Our kernels are relinked from the binary stock kernel, so the AMIX Installer
links the driver in:

| file | what |
|---|---|
| `port/src/a4091_kit.c` | compiles `a4091-wr.c` and adds the `sdcardbase()` it needs (as `a4091_sdcardbase`): the queue functions `a4091q`, `a4092q`, `a4770q` look the board up with `autocon()` by product and sd.c's index, record its base for the card, and call `a4091queue()` |
| `port/src/a4091_card.s` | sd.c's controller table with six rows: the stock three, the A4091, the A4092, the A4770 |
| `port/src/patch_a4091_card.py` | retargets sd.c `init()` to that table and its loop bound 2 -> 5, asserted |
| `port/make-kit-030.sh` | the kit for the stock 68030 kernel: the static driver, boards mapped with `sptalloc()`, no cache maintenance (as the stock `a2091.c`) |
| `port/make-kit-040.sh` | the kit for the 68040/68060 port: `-DA4091_KVA -DA4091_D42`, boards mapped noncacheable by the port's `dev_kvmap` (a private copy, `a4091_kvmap`), every transfer through the port kernel's D42 service `dma_prepare`/`dma_complete` (`port/src/dma_cache040.s`) |

Both builds check what the object imports and defines, and run upstream's
width check (8- and 32-bit accesses only on the 53C710); the 040 build also
the port's 68060 instruction scan and that no `sptalloc()` or fixed
0x40000000 is left.

**Polled I/O**, as upstream's static driver: no interrupt handler, no init;
a board is mapped on its first request.

**One table kit a kernel**: the buddha, z3660scsi and a4091 kits each bring
their own controller table (`KIT.info conflicts=`; the patcher refuses a table
that is another kit's).  sd.c takes two controllers (`SDCARDS`), so an A3000
holds its own SCSI and one such card anyway.

## Status

EMU only: Amiberry 8.5.0, A3000 (own SCSI on, no disk on it) with an A4091
and the AMIX core disk at ID 6 on it, root and swap stamped to the A4091
(`ced0s1`), the kernel booted by pvmk over serial.  The stock 2.1p2a kernel and
the generic 68040 port kernel with this kit both reach the login prompt; the
040 one reports `a4091: card 1: board at 0x40000000, NCS pages, D42 seam
linked`.  fsck of the root on the A4091 after an unclean stop ran and repaired
it.  The emulator's data cache was off, so the 040 kit's cache maintenance is
reached, not proven.  Upstream ran the 040/060 arm with root on a real A4092
(A4000D, Z3660); this kit's build of it has not run on hardware.
