# buddha: the Buddha and Catweasel Z-II IDE for AMIX

The Individual Computers **Buddha** (Zorro II, two IDE ports, AutoConfig
0x1212/0x00) and **Catweasel Z-II** (three IDE ports, 0x1212/0x2A) as
controllers of AMIX's own SCSI disk layer: their drives are ordinary dd
disks, block major 18 / character major 40, `/dev/dsk/cNdUsS` and
`/dev/rdsk/cNdUsS`.  The units are fixed per port and drive: port 0 master
6, slave 5; port 1 master 3, slave 2; port 2 (Catweasel only) 1 and 0.  So a
core disk on port 0's master roots on `c<card>6d0s1` as it does at SCSI ID 6.

## Where it comes from

`src/ide.c` and `src/buddha.c` are **amix-gayle-ide** by Emre Bastuz (MIT,
`LICENSE`), the amigaux.org fork at the commit `UPSTREAM` names, unchanged and
pinned by sha256 in the kit scripts.  Upstream builds the Buddha back end only
as an amigaux 1.1 loadable module (`buddha`): it finds the boards with
`autocon()`, probes the ports, and registers each board through the module
core (`sd_register`, `root_defer`, `intr_attach`).

## The kit

Our kernels have no module core, so the AMIX Installer links the driver in:

| file | what |
|---|---|
| `port/src/buddha_kit.c` | compiles `ide.c` as its Buddha module form and defines the four module-core calls; `buddhainit()` runs `buddha_load("intr=0")` from the installer's io_init chain; `budqueue()`/`cwqueue()` map sd.c's board index per product to the driver's board |
| `port/src/include/sys/` | stand-ins for the module kit's `moddefs.h` and `amixddi.h` |
| `port/src/buddha_card.s` | sd.c's controller table with five rows: the stock three, the Buddha, the Catweasel Z-II |
| `port/src/patch_buddha_card.py` | retargets sd.c `init()` to that table and its loop bound 2 -> 4, asserted |
| `port/make-kit-030.sh`, `port/make-kit-040.sh` | the kits for the stock 68030 kernel and the 68040/68060 port (Model B, `-DIDE_CACHE=1 -DIDE_NBPP=4096`) |

**Polled I/O.**  Interrupts need the module core's deferred self-test
(process context after the root is mounted), so the kit loads the driver with
`intr=0`, the mode the module falls back to when its self-test fails.

**Not with the z3660scsi kit yet**: each brings its own controller table
(`KIT.info conflicts=z3660_scsicard`; the patcher refuses the second).

## Status

EMU only, Amiberry 8.5.0, A3000 with a Buddha, the stock 2.1p2a kernel with
this kit, root and swap stamped to the Buddha (`ced0s1`), the kernel booted
by pvmk over serial: see the commit that adds this directory for what was
seen.  Not run on a real Buddha or Catweasel.
