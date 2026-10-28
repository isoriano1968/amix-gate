| serdbg_keepbaud.s -- mirror kernel console output to the Amiga serial port
| (conputc hook), at the speed the boot loader left the port at.
|
| src/serdbg.s with one difference: serdbg.s sets SERPER to 9600 baud on the
| first character; this one leaves SERPER alone, so a kernel started by
| SORIX-ILOM-LDOM (or any loader that set the port up) keeps that speed and
| the serial console stays readable.  Linked in by relink-040-serkeep.sh.
| Each character: wait for TBE (bounded), write SERDAT with interrupts
| masked, then the real coputc() for the screen -- as serdbg.s (ISSUE-23).

	.text
	.globl	serdbg_putc
serdbg_putc:
	linkw	%fp,&0
	moveml	%d2/%a2,%sp@-
	movel	%fp@(8),%d2		| d2 = ch
	movel	&0x00dff000,%a2		| custom-chip base
	tstw	Lser_init
	bnew	Lsd_go
	movew	&1,Lser_init
	| serper: kept as the loader (SORIX-ILOM-LDOM) set it
Lsd_go:
	movew	%sr,%sp@-		| save IPL (supervisor ctx always)
	oriw	&0x0700,%sr		| mask ints: wait+write must be atomic
	movel	&0x00010000,%d0		| bounded TBE wait BEFORE write
Lsd_wait:
	movew	%a2@(0x18),%d1
	andiw	&0x2000,%d1
	bnew	Lsd_send
	subql	&1,%d0
	bnew	Lsd_wait
Lsd_send:
	movew	%d2,%d1
	andiw	&0x00ff,%d1
	oriw	&0x0100,%d1		| STOPBIT | ch
	movew	%d1,%a2@(0x30)		| serdat <- byte (buffer known empty)
	movew	%sp@+,%sr		| restore IPL
Lsd_scr:
	movel	%d2,%sp@-		| coputc(ch) -- real screen output
	jsr	coputc
	addqw	&4,%sp
	moveml	%sp@+,%d2/%a2
	unlk	%fp
	rts
	nop

	.balign 4			| pad section to a 4-byte multiple (bss placement: rel.c puts .bss at data_end UNALIGNED)
	.data
	.globl	conputc
conputc:
	.long	serdbg_putc
Lser_init:
	.word	0
	.even
	.balign 4			| pad section to a 4-byte multiple (bss placement: rel.c puts .bss at data_end UNALIGNED)
