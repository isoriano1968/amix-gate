| memspan.s -- one page array over several memory regions, step 2 of
| docs/MEMORY-REGIONS-PLAN.md (2026-11-07).
|
| Stock kvm_init builds the page array from the first free page after the
| kernel (ff) to maxclick, inline at 0x48e18-0x48ebc.  tools/patch-memspan.py
| puts a `bra.l kvm_span_tail` at 0x48e18; mspan_build() below does the same
| work for an array that starts at the lowest usable region below the kernel
| (the A3000 motherboard RAM at 0x07000000):
|
|   pages_base = lo (lowest region), entries = maxclick - lo
|   array + hash physically at ff, as stock (DDR, not slow motherboard RAM)
|   page_init(pp, entries, lo): every entry locked (p_lock)
|   memialloc(region) for each extra region FIRST, then memialloc(ff', maxclick):
|     page_free(pp, 1) puts each page at the free list head, so the DDR pages
|     freed last are handed out first and the slow ones last
|
| The pages between them (kernel image, ksegmappt/ksegupt, the array) keep
| page_init's locked, ownerless entries; pageout's checkpage and fsflush skip
| such entries.  They are not "managed": every stock and port range check
| `cmpl pages_base,%dN; bcs not_vm_page` becomes `bsr.l pfnchk_dN; bcs ...`
| (patcher), which answers from mm_lo/mm_hi, the ranges memialloc freed.  The
| following `cmpl pages_end` stays: a managed pfn is always below it.
|
| The page-table plausibility guards' `movel #_start,%dN` become
| `movel mspan_loaddr,%dN` (patcher), so page tables in motherboard RAM pass.
| Regions above the kernel region (step 3, 2026-11-07: the Z3660's Zorro III
| RAM, moved to 0x20000000 by pvmk's z3reloc) widen the array to their end:
| entries = top - lo, they are freed after the motherboard RAM and before the
| kernel region (so CPU RAM is handed out first), and maxclick becomes top.
| The pages between maxclick and such a region are a hole: locked entries,
| never freed, never managed.
| Wiring: relink-040-memspan.sh (--globalize-symbol kvm_init, ld -r, patcher).
| Old gas: '&' immediates, '|' comments, no macros.

	.text
	.globl	kvm_span_tail
	.globl	mspan_build
	.globl	pfn_managed
	.globl	mm_n
	.globl	mm_lo
	.globl	mm_hi
	.globl	mspan_lo
	.globl	mspan_extra
	.globl	mspan_above
	.globl	mspan_top
	.globl	mspan_sink
	.globl	mspan_loaddr
	.globl	pfnchk_d0
	.globl	pfnchk_d1
	.globl	pfnchk_d2
	.globl	pfnchk_d3
	.globl	pfnchk_d4
	.globl	pfnchk_d5
	.globl	pfnchk_d6
	.globl	pfnchk_d7

MM_MAX	=	8

| Entered by bra.l from kvm_init+0x1ea (0x48e18) with kvm_init's frame.  The
| stock block it replaces leaves 36 bytes of call arguments on the stack
| (popped by `addaw #36,%sp` at 0x48ed0) and d5 = first free pfn after the
| array; we leave the same.
kvm_span_tail:
	movel	eksegupt,%d0
	addil	&4095,%d0
	lsrl	&8,%d0
	lsrl	&4,%d0
	movel	%d0,%sp@-
	jsr	mspan_build
	addqw	&4,%sp
	movel	%d0,%d5
	subaw	&36,%sp
	jmp	kvm_init+0x28e

| int mspan_build(ff) -> first free pfn after the array
mspan_build:
	linkw	%fp,&0
	moveml	%d2-%d7/%a2,%sp@-
	movel	%fp@(8),%d7		| d7 = ff
| lo = lowest base of an extra region (RAM, not the kernel's, ending at or
| below ff)
	movel	%d7,%d6
	lea	memregion,%a2
	movel	memregion_n,%d2
	bras	Lmb_lo_cond
Lmb_lo_loop:
	bsr	mb_extra		| d0 = base pfn, d1 = end pfn, Z set if not extra
	beqs	Lmb_lo_next
	cmpl	%d6,%d0
	bccs	Lmb_lo_next
	movel	%d0,%d6
Lmb_lo_next:
	addaw	&12,%a2
	subql	&1,%d2
Lmb_lo_cond:
	tstl	%d2
	bgts	Lmb_lo_loop
	movel	%d6,mspan_lo
| mspan_loaddr = min(lo, _start>>12) << 12: the lowest address the page-table
| plausibility guards (hat_unload, krnxmemflt, i10rev) accept, which stock had
| as _start (static kernel tables sit between _start and ff)
	movel	%d6,%d0
	lsll	&8,%d0
	lsll	&4,%d0
	lea	_start,%a0
	cmpal	%d0,%a0
	bccs	1f
	movel	%a0,%d0
1:	movel	%d0,mspan_loaddr
| top = the end of the highest RAM region above the kernel's, at least
| maxclick (step 3)
	movel	maxclick,%d4
	lea	memregion,%a2
	movel	memregion_n,%d2
	bras	Lmb_top_cond
Lmb_top_loop:
	bsr	mb_above		| d0 = base pfn, d1 = end pfn, Z set if not above
	beqs	Lmb_top_next
	cmpl	%d4,%d1
	blss	Lmb_top_next
	movel	%d1,%d4
Lmb_top_next:
	addaw	&12,%a2
	subql	&1,%d2
Lmb_top_cond:
	tstl	%d2
	bgts	Lmb_top_loop
	movel	%d4,mspan_top
| entries = top - lo
	subl	%d6,%d4
| page_hashsz: entries / 4 rounded up to a power of two (stock loop)
	movel	%d4,%d1
	lsrl	&2,%d1
	bnes	1f
	moveq	&1,%d1
1:	movel	%d1,%d0
	subql	&1,%d0
	andl	%d1,%d0
	beqs	2f
	movel	%d1,%d0
	subql	&1,%d0
	orl	%d1,%d0
	movel	%d0,%d1
	addql	&1,%d1
	bras	1b
2:	movel	%d1,page_hashsz
| npages = (hashsz * 4 + entries * 60 + 4095) >> 12
	movel	%d1,%d3
	asll	&2,%d3
	moveq	&60,%d2
	mulsl	%d4,%d2			| d2 = entries * 60
	addl	%d2,%d3
	addil	&4095,%d3
	lsrl	&8,%d3
	lsrl	&4,%d3
| pp = sptalloc(npages, 1, ff, 0): the array lives at ff, as stock
	clrl	%sp@-
	movel	%d7,%sp@-
	pea	1
	movel	%d3,%sp@-
	jsr	sptalloc
	lea	%sp@(16),%sp
	moveal	%d0,%a2
	addl	%d2,%d0
	movel	%d0,page_hash
	addl	%d3,%d7			| d7 = first free pfn after the array
	jsr	hat_init
| page_init(pp, entries, lo)
	movel	%d6,%sp@-
	movel	%d4,%sp@-
	movel	%a2,%sp@-
	jsr	page_init
	lea	%sp@(12),%sp
| free the extra (slow) regions first, then the kernel region's rest
	clrl	mm_n
	clrl	mspan_extra
	moveq	&0,%d5			| d5 = pages freed
	lea	memregion,%a2
	movel	memregion_n,%d2
	bras	Lmb_fr_cond
Lmb_fr_loop:
	bsr	mb_extra
	beqs	Lmb_fr_next
	movel	%d0,%d3
	movel	%d1,%d4
	movel	%d4,%sp@-
	movel	%d3,%sp@-
	jsr	memialloc
	addqw	&8,%sp
	movel	%d3,%d0
	movel	%d4,%d1
	bsr	mm_add
	movel	%d4,%d0
	subl	%d3,%d0
	addl	%d0,%d5
	addl	%d0,mspan_extra
Lmb_fr_next:
	addaw	&12,%a2
	subql	&1,%d2
Lmb_fr_cond:
	tstl	%d2
	bgts	Lmb_fr_loop
| then the regions above the kernel's (Z3 RAM), before the kernel region
	lea	memregion,%a2
	movel	memregion_n,%d2
	bras	Lmb_ab_cond
Lmb_ab_loop:
	bsr	mb_above
	beqs	Lmb_ab_next
	movel	%d0,%d3
	movel	%d1,%d4
	movel	%d4,%sp@-
	movel	%d3,%sp@-
	jsr	memialloc
	addqw	&8,%sp
	movel	%d3,%d0
	movel	%d4,%d1
	bsr	mm_add
	movel	%d4,%d0
	subl	%d3,%d0
	addl	%d0,%d5
	addl	%d0,mspan_extra
	addl	%d0,mspan_above
Lmb_ab_next:
	addaw	&12,%a2
	subql	&1,%d2
Lmb_ab_cond:
	tstl	%d2
	bgts	Lmb_ab_loop
	movel	maxclick,%sp@-
	movel	%d7,%sp@-
	jsr	memialloc
	addqw	&8,%sp
	movel	%d7,%d0
	movel	maxclick,%d1
	bsr	mm_add
	movel	maxclick,%d0
	subl	%d7,%d0
	addl	%d0,%d5
	movel	%d5,maxmem
| SVR4.0's anon_resv also needs anoninfo.ani_max (only swapadd raises it, by
| the swap size), so without this the processes together could reserve no
| more anonymous memory than swap, whatever the RAM (6 x 64 MB failed with
| 200 MB swap and 528 MB RAM, 2026-11-07).  The RAM counts as well, as
| Solaris' swapfs does; availsmem (RAM + swap, set from maxmem and by
| swapadd) still bounds the total.
	addl	%d5,anoninfo
	movel	mspan_extra,%d0
	addl	%d0,physmem
	movel	mspan_top,maxclick	| the array's end (step 3)
	movel	%d7,%d0
	moveml	%fp@(-28),%d2-%d7/%a2
	unlk	%fp
	rts

| mb_extra: a2 -> memregion entry, d6/d7 = current lo / ff.  An extra region
| is MRF_RAM, not MRF_KERNEL, and ends at or below ff (below the kernel).
| Out: d0 = base pfn, d1 = end pfn, Z clear if extra.  Uses d0/d1 only.
mb_extra:
	movel	%a2@(8),%d0
	btst	&1,%d0			| MRF_KERNEL
	bnes	Lmx_no
	btst	&0,%d0			| MRF_RAM
	beqs	Lmx_no
	movel	%a2@(4),%d1
	lsrl	&8,%d1
	lsrl	&4,%d1
	cmpl	%d7,%d1
	bhis	Lmx_no
	movel	%a2@,%d0
	lsrl	&8,%d0
	lsrl	&4,%d0
	cmpl	%d0,%d1
	blss	Lmx_no
	movel	%a2@(4),%d1
	lsrl	&8,%d1
	lsrl	&4,%d1
	andi	&0xfb,%ccr		| Z clear: extra
	rts
Lmx_no:
	ori	&4,%ccr			| Z set: not extra
	rts

| mb_above: a2 -> memregion entry.  A region above is MRF_RAM, not
| MRF_KERNEL, and starts at or above maxclick (the kernel region's end).
| Out: d0 = base pfn, d1 = end pfn, Z clear if above.  Uses d0/d1 only.
mb_above:
	movel	%a2@(8),%d0
	btst	&1,%d0			| MRF_KERNEL
	bnes	Lma_no
	btst	&0,%d0			| MRF_RAM
	beqs	Lma_no
	movel	%a2@,%d0
	lsrl	&8,%d0
	lsrl	&4,%d0
	cmpl	maxclick,%d0
	bcss	Lma_no			| starts below maxclick
	movel	%a2@(4),%d1
	lsrl	&8,%d1
	lsrl	&4,%d1
	cmpl	%d0,%d1
	blss	Lma_no
	andi	&0xfb,%ccr		| Z clear: above
	rts
Lma_no:
	ori	&4,%ccr			| Z set: not above
	rts

| mm_add: record [d0, d1) as managed.  Uses a0/d0/d1.
mm_add:
	movel	%d2,%sp@-
	movel	mm_n,%d2
	cmpl	&MM_MAX,%d2
	bccs	1f
	asll	&2,%d2
	lea	mm_lo,%a0
	movel	%d0,%a0@(0,%d2:l)
	lea	mm_hi,%a0
	movel	%d1,%a0@(0,%d2:l)
	addql	&1,mm_n
1:	movel	%sp@+,%d2
	rts

| int pfn_managed(pfn): 1 if pfn went to the free list at boot.
pfn_managed:
	movel	%sp@(4),%d0
	moveml	%d1/%a0-%a1,%sp@-
	bsr	mm_test
	movel	%d1,%d0
	moveml	%sp@+,%d1/%a0-%a1
	moveal	%d0,%a0
	rts

| mm_test: d0 = pfn -> d1 = 1 managed / 0 not.  Uses d1/a0/a1.
mm_test:
	lea	mm_lo,%a0
	movel	mm_n,%d1
	asll	&2,%d1
	lea	mm_lo,%a1
	addal	%d1,%a1			| a1 = end of mm_lo
1:	cmpal	%a1,%a0
	bccs	3f
	cmpl	%a0@,%d0
	bcss	2f
	cmpl	%a0@(MM_MAX*4),%d0	| mm_hi[i]
	bcss	4f
2:	addql	&4,%a0
	bras	1b
3:	moveq	&0,%d1
	rts
4:	moveq	&1,%d1
	rts

| pfnchk_dN: carry set if the pfn in %dN is NOT a managed page; every
| register is kept (moveml and rts leave the condition codes alone).
pfnchk_d0:
	moveml	%d0-%d1/%a0-%a1,%sp@-
	bras	Lpc_go
pfnchk_d1:
	moveml	%d0-%d1/%a0-%a1,%sp@-
	movel	%d1,%d0
	bras	Lpc_go
pfnchk_d2:
	moveml	%d0-%d1/%a0-%a1,%sp@-
	movel	%d2,%d0
	bras	Lpc_go
pfnchk_d3:
	moveml	%d0-%d1/%a0-%a1,%sp@-
	movel	%d3,%d0
	bras	Lpc_go
pfnchk_d4:
	moveml	%d0-%d1/%a0-%a1,%sp@-
	movel	%d4,%d0
	bras	Lpc_go
pfnchk_d5:
	moveml	%d0-%d1/%a0-%a1,%sp@-
	movel	%d5,%d0
	bras	Lpc_go
pfnchk_d6:
	moveml	%d0-%d1/%a0-%a1,%sp@-
	movel	%d6,%d0
	bras	Lpc_go
pfnchk_d7:
	moveml	%d0-%d1/%a0-%a1,%sp@-
	movel	%d7,%d0
Lpc_go:
	bsr	mm_test
	cmpl	&1,%d1			| managed: 1-1, no borrow; not: 0-1, C set
	moveml	%sp@+,%d0-%d1/%a0-%a1
	rts

| The patcher points each relocation of a replaced instruction here, so the
| loader's fixups land in this table instead of the new code.
	.even
mspan_sink:
	.space	256

	.data
	.align	4
mm_n:	.long	0
mm_lo:	.space	MM_MAX*4
mm_hi:	.space	MM_MAX*4
mspan_lo:	.long	0
mspan_extra:	.long	0
mspan_above:	.long	0
mspan_top:	.long	0
mspan_loaddr:	.long	0
