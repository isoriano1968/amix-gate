| z3660scsi_card040.s -- the four-row scsicard[] of the z3660scsi kit-040:
| the stock three controllers of sd.c, then the Z3660.
|
| sd.c's local init() walks scsicard[] (three 12-byte rows {product, queue
| function, hardware name}) and calls autocon() for each product; every board
| found goes into queue[] (SDCARDS 2), sorted by board address, and its index
| there is the "card" of the dd minor ((slice << 4) | (card << 3) | unit).
| The table cannot grow in place, so patch_z3660scsi_card.py retargets the
| `lea scsicard,%a3` relocation of init() to this copy and raises the loop
| bound from 2 to 3 -- the registration of the A3000 + Z3660 kernel
| (relink-040-z3660.sh + z3660_glue040.s), without its parinit hook (that one
| stores the ethernet's cdevsw[51].d_str) and without the dd.c ordering island
| (off there too, Z3660_DD_ORDER unset).
|
| Rows 0-2 bind to the stock queue functions and carry the stock hardware
| names (DIOCHARDWARENAME); the patcher asserts both against the base before
| it retargets anything.
|
| Data only; ends with .balign 4.  Assemble: m68k-cbm-sysv4-as.

	.data
	.balign	4
	.globl	z3660_scsicard
z3660_scsicard:
	.long	0x0202f003
	.long	a3091queue
	.long	Lzsc_n0
	.long	0x02020001
	.long	a2090queue
	.long	Lzsc_n1
	.long	0x02020003
	.long	a2091queue
	.long	Lzsc_n2
	.long	0x144b0001
	.long	z3660queue
	.long	Lzsc_n3
Lzsc_n0:
	.asciz	"A3000 Internal SCSI"
Lzsc_n1:
	.asciz	"A2090 SCSI"
Lzsc_n2:
	.asciz	"A2091 SCSI"
Lzsc_n3:
	.asciz	"Z3660 SCSI"
	.balign	4
