| a4091_card.s -- the six-row scsicard[] of the a4091 kit (both kernel
| lines): sd.c's stock three controllers, then the NCR 53C710 Zorro III
| boards: Commodore A4091 (AutoConfig 0x0202/0x54), A4092 and A4770
| (SCSI.ME, 0xc0de/0x01 and 0x02).
|
| sd.c's local init() walks scsicard[] (12-byte rows {product, queue
| function, hardware name}) with autocon(); every board found goes into
| queue[] (SDCARDS 2), sorted by board address, and its index there is the
| card of the dd minor.  The table cannot grow in place: patch_a4091_card.py
| retargets init()'s `lea scsicard,%a3` to this copy and raises the loop
| bound from 2 to 5.  Rows 0-2 bind to the stock queue functions with the
| stock hardware names (the patcher asserts both against the base).
|
| Data only; ends with .balign 4.  Assemble: m68k-cbm-sysv4-as.

	.data
	.balign	4
	.globl	a4091_scsicard
a4091_scsicard:
	.long	0x0202f003
	.long	a3091queue
	.long	Lasc_n0
	.long	0x02020001
	.long	a2090queue
	.long	Lasc_n1
	.long	0x02020003
	.long	a2091queue
	.long	Lasc_n2
	.long	0x02020054
	.long	a4091q
	.long	Lasc_n3
	.long	0xc0de0001
	.long	a4092q
	.long	Lasc_n4
	.long	0xc0de0002
	.long	a4770q
	.long	Lasc_n5
Lasc_n0:
	.asciz	"A3000 Internal SCSI"
Lasc_n1:
	.asciz	"A2090 SCSI"
Lasc_n2:
	.asciz	"A2091 SCSI"
Lasc_n3:
	.asciz	"A4091 SCSI"
Lasc_n4:
	.asciz	"A4092 SCSI"
Lasc_n5:
	.asciz	"A4770 SCSI"
	.balign	4
