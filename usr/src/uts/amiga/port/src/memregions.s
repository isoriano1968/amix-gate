| memregions.s -- the kernel's memory region table, step 1 of
| docs/MEMORY-REGIONS-PLAN.md (2026-11-06).
|
| AMIX's config() keeps exactly one region of bootinfo.memory[] (the one the
| kernel sits in: MAINSTORE, VSIZOFMEM) and forgets the others.  This unit
| records them all, Linux-memblock style, before the VM is built:
|
|   memregion[MR_MAX] = { base, end, flags }   sorted by base, page aligned
|   memregion_n, memregion_drop (entries that did not fit)
|   pfn_valid(pfn)  -> 1 if the 4 KB frame is RAM in a usable region
|
| Step 1 changes nothing the kernel does with memory: MAINSTORE/VSIZOFMEM, the
| page array and the free list are still config()'s single region.  It only
| builds the table (mlsetup wrapper, before kvm_init needs it in step 2) and
| prints it once the console runs (startup wrapper).
|
| Region rules:
|   * chip RAM (MEMF_CHIP)                -> MRF_CHIP, not RAM for pfn_valid
|   * base at or above 0x40000000         -> MRF_HIGH: inside the kernel's
|     virtual region 1, not reachable 1:1 through ITT0/DTT0
|   * the rest                            -> MRF_RAM, end clipped to 0x40000000;
|     MRF_KERNEL on the region holding MAINSTORE
|   * base/end rounded DOWN to 4 KB: the AmigaOS MemHeader at the start of a
|     region (lower = base + 0x20) is RAM too, and adjacent regions then meet.
|
| Wiring (relink-040-memregions.sh): --weaken-symbol mlsetup, startup and
| --add-symbol mlsetup_orig/startup_orig on the stock bodies.  mlsetup bzeroes
| edata..end, so the table lives in .data.  Old gas: '&' immediates, '|' comments.

	.text
	.globl	mlsetup
	.globl	startup
	.globl	pfn_valid
	.globl	memregions_scan
	.globl	memregion
	.globl	memregion_n
	.globl	memregion_drop

MR_MAX	=	8
MR_SIZE	=	12
MRF_RAM	=	1
MRF_KERNEL =	2
MRF_HIGH =	4
MRF_CHIP =	8
BI_MEM	=	0x440
MH_ATTR	=	14
MH_LOWER =	20
MH_UPPER =	24
HIGH	=	0x40000000

| mlsetup(firstfree): table first, then the stock body with the same frame.
mlsetup:
	jsr	memregions_scan
	jmp	mlsetup_orig

memregions_scan:
	moveml	%d2-%d5/%a2,%sp@-
	lea	bootinfo+BI_MEM,%a2
	moveq	&15,%d5
	clrl	memregion_n
	clrl	memregion_drop
Lmr_next:
	movel	%a2@(MH_LOWER),%d0
	movel	%a2@(MH_UPPER),%d1
	andil	&0xfffff000,%d0
	andil	&0xfffff000,%d1
	cmpl	%d0,%d1
	blsw	Lmr_skip
	movew	%a2@(MH_ATTR),%d3
	btst	&1,%d3
	beqs	1f
	moveq	&MRF_CHIP,%d2
	bras	Lmr_add
1:	cmpil	&HIGH,%d0
	bcss	2f
	moveq	&MRF_HIGH,%d2
	bras	Lmr_add
2:	cmpil	&HIGH,%d1
	blss	3f
	movel	&HIGH,%d1
3:	moveq	&MRF_RAM,%d2
	movel	MAINSTORE,%d3
	cmpl	%d0,%d3
	bcss	Lmr_add
	cmpl	%d1,%d3
	bccs	Lmr_add
	moveq	&MRF_RAM+MRF_KERNEL,%d2
Lmr_add:
	movel	memregion_n,%d3
	cmpl	&MR_MAX,%d3
	bcss	4f
	addql	&1,memregion_drop
	bras	Lmr_skip
4:	lea	memregion,%a1
	movel	%d3,%d4
	asll	&2,%d4
	addal	%d4,%a1
	addal	%d4,%a1
	addal	%d4,%a1
	lea	memregion,%a0
5:	cmpal	%a0,%a1
	beqs	6f
	cmpl	%a1@(-12),%d0
	bccs	6f
	movel	%a1@(-12),%a1@
	movel	%a1@(-8),%a1@(4)
	movel	%a1@(-4),%a1@(8)
	subaw	&MR_SIZE,%a1
	bras	5b
6:	movel	%d0,%a1@
	movel	%d1,%a1@(4)
	movel	%d2,%a1@(8)
	addql	&1,memregion_n
Lmr_skip:
	addaw	&32,%a2
	dbf	%d5,Lmr_next
	moveml	%sp@+,%d2-%d5/%a2
	rts

| int pfn_valid(pfn): the 4 KB frame lies in an MRF_RAM region.
pfn_valid:
	movel	%sp@(4),%d0
	lsll	&8,%d0
	lsll	&4,%d0
	lea	memregion,%a0
	movel	memregion_n,%d1
	beqs	Lpv_no
	subql	&1,%d1
1:	btst	&0,%a0@(11)
	beqs	2f
	cmpl	%a0@,%d0
	bcss	2f
	cmpl	%a0@(4),%d0
	bcss	Lpv_yes
2:	addaw	&MR_SIZE,%a0
	dbf	%d1,1b
Lpv_no:
	moveq	&0,%d0
	moveal	%d0,%a0
	rts
Lpv_yes:
	moveq	&1,%d0
	moveal	%d0,%a0
	rts

| startup(): the stock body, then the table on the console.
startup:
	jsr	startup_orig
	moveml	%d2/%a2,%sp@-
	lea	memregion,%a2
	moveq	&0,%d2
	bras	Lst_cond
Lst_loop:
	movel	%a2@(8),%d0
	lea	mr_s_chip,%a0
	btst	&3,%d0
	bnes	1f
	lea	mr_s_high,%a0
	btst	&2,%d0
	bnes	1f
	lea	mr_s_ram,%a0
	btst	&1,%d0
	beqs	1f
	lea	mr_s_kernel,%a0
1:	movel	%a0,%sp@-
	movel	%a2@(4),%d0
	subql	&1,%d0
	movel	%d0,%sp@-
	movel	%a2@,%sp@-
	movel	%d2,%sp@-
	pea	mr_fmt
	jsr	printf
	lea	%sp@(20),%sp
	addaw	&MR_SIZE,%a2
	addql	&1,%d2
Lst_cond:
	cmpl	memregion_n,%d2
	bcss	Lst_loop
	tstl	memregion_drop
	beqs	2f
	movel	memregion_drop,%sp@-
	pea	mr_fmt_drop
	jsr	printf
	addqw	&8,%sp
2:	moveml	%sp@+,%d2/%a2
	rts

mr_fmt:	.asciz	"memregion %d: %x-%x %s\n"
mr_fmt_drop:	.asciz	"memregion: %d more regions dropped (table full)\n"
mr_s_kernel:	.asciz	"RAM, kernel (MAINSTORE)"
mr_s_ram:	.asciz	"RAM, not used yet"
mr_s_high:	.asciz	"above 1 GB, not reachable"
mr_s_chip:	.asciz	"chip"
	.even

	.data
	.align	4
memregion_n:	.long	0
memregion_drop:	.long	0
memregion:	.space	96
