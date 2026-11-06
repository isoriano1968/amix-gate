| kmemchk.s -- debug guard on the kernel's fast free lists (2026-11-06).
|
| At 256 MB, init's address spaces came from as_freelist at 0x00000200 (chip
| RAM, the AmigaOS vector table) and then 0x00F833A4 (Kickstart ROM); every
| legitimate kmem address is kernel virtual, >= 0x40000000.  These wrappers
| panic, with the kernel's backtrace, the moment such an address is handed in
| (kmem_fast_free), found at a list head, or handed out (kmem_fast_alloc), so
| the backtrace names the code that brought it in.
|
| Wiring (relink-040-kmemchk.sh): --weaken-symbol kmem_fast_alloc/_free and
| --add-symbol kmem_fast_alloc_orig/_free_orig on the stock bodies.

	.text
	.globl	kmem_fast_alloc
	.globl	kmem_fast_free
	.globl	kmc_magic

KMC_LOW	=	0x40000000

| kmem_fast_alloc(caddr_t *flist, u_int size, u_int nfast, int flag)
kmem_fast_alloc:
	linkw	%fp,&0
	movel	%fp@(8),%a0
	movel	%a0@,%d0
	beqs	1f
	cmpil	&KMC_LOW,%d0
	bcsw	kmc_badhead
1:	movel	%fp@(20),%sp@-
	movel	%fp@(16),%sp@-
	movel	%fp@(12),%sp@-
	movel	%fp@(8),%sp@-
	jsr	kmem_fast_alloc_orig
	lea	%sp@(16),%sp
	tstl	%d0
	beqs	2f
	cmpil	&KMC_LOW,%d0
	bcsw	kmc_badret
	movel	%fp@(8),%a0
	movel	%a0@,%d1
	beqs	2f
	cmpil	&KMC_LOW,%d1
	bcsw	kmc_badnext
2:	moveal	%d0,%a0
	unlk	%fp
	rts

| kmem_fast_free(caddr_t *flist, caddr_t p)
kmem_fast_free:
	linkw	%fp,&0
	movel	%fp@(12),%d0
	beqs	1f
	cmpil	&KMC_LOW,%d0
	bcsw	kmc_badfree
1:	movel	%fp@(8),%a0
	movel	%a0@,%d0
	beqs	2f
	cmpil	&KMC_LOW,%d0
	bcsw	kmc_badhead
2:	movel	%fp@(12),%sp@-
	movel	%fp@(8),%sp@-
	jsr	kmem_fast_free_orig
	addqw	&8,%sp
	unlk	%fp
	rts

kmc_badhead:
	pea	kmc_s_head
	bras	kmc_panic
kmc_badret:
	pea	kmc_s_ret
	bras	kmc_panic
kmc_badnext:
	movel	%d1,%d0
	pea	kmc_s_next
	bras	kmc_panic
kmc_badfree:
	pea	kmc_s_free
kmc_panic:
	movel	%sp@+,%a1
	movel	%fp@(4),%sp@-		| caller of the wrapper
	movel	%d0,%sp@-		| the bad address
	movel	%fp@(8),%sp@-		| the free list
	movel	%a1,%sp@-
	pea	kmc_fmt
	pea	3			| CE_PANIC
	jsr	cmn_err
3:	bras	3b

kmc_fmt:	.asciz	"kmemchk: %s list %x addr %x from %x"
kmc_s_head:	.asciz	"bad list head"
kmc_s_ret:	.asciz	"bad block handed out"
kmc_s_next:	.asciz	"bad next link"
kmc_s_free:	.asciz	"bad block freed"
	.even
kmc_magic:	.ascii	"KMC!"
