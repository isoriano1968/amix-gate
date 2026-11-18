| buddha_card.s -- the five-row scsicard[] of the buddha kit (both kernel
| lines): sd.c's stock three controllers, then the Individual Computers
| Buddha (AutoConfig 0x1212/0x00) and Catweasel Z-II (0x1212/0x2A).
|
| sd.c's local init() walks scsicard[] (12-byte rows {product, queue
| function, hardware name}) with autocon(); every board found goes into
| queue[] (SDCARDS 2), sorted by board address, and its index there is the
| card of the dd minor.  The table cannot grow in place: patch_buddha_card.py
| retargets init()'s `lea scsicard,%a3` to this copy and raises the loop
| bound from 2 to 4.  Rows 0-2 bind to the stock queue functions with the
| stock hardware names (the patcher asserts both against the base).
|
| Data only; ends with .balign 4.  Assemble: m68k-cbm-sysv4-as.

	.data
	.balign	4
	.globl	buddha_scsicard
buddha_scsicard:
	.long	0x0202f003
	.long	a3091queue
	.long	Lbsc_n0
	.long	0x02020001
	.long	a2090queue
	.long	Lbsc_n1
	.long	0x02020003
	.long	a2091queue
	.long	Lbsc_n2
	.long	0x12120000
	.long	budqueue
	.long	Lbsc_n3
	.long	0x1212002a
	.long	cwqueue
	.long	Lbsc_n4
Lbsc_n0:
	.asciz	"A3000 Internal SCSI"
Lbsc_n1:
	.asciz	"A2090 SCSI"
Lbsc_n2:
	.asciz	"A2091 SCSI"
Lbsc_n3:
	.asciz	"Buddha IDE"
Lbsc_n4:
	.asciz	"Catweasel IDE"
	.balign	4
