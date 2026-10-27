| kvseg040.s -- a larger kernel heap area (kvseg) for machines with a lot of RAM.
|
| kvseg (VA syssegs = 0x40040000) is where sptalloc/kmem_alloc's big pool get
| their kernel virtual addresses, and the page structure array lives there too.
| Stock size: 4 MB.  With 128 MB of RAM (a Z3660's CPU RAM) the page array alone
| takes 1.9 MB and boot-time allocations the rest: sptmap is left with one page
| and the first big kmem_alloc (every fork) sleeps on Km_BigWanted for ever.
|
| src/patch_kvseg.py grows kvseg to KVSEG_MB (default 12; 16 for a Z3660
| with its RTG console mapped): sysseginit's page-table loop, the
| sptmap mfree count and kvm_init's seg_attach size, and moves kvsegmap (the
| segmap window after kvseg) from 0x40440000 to 0x40c40000.  segmap's size,
| smsegs, scales with physical memory (about 126 MB for 128 MB of RAM); the
| window up to kvsegu (0x48440000) is now 120 MB, so smsegs is clamped here:
| kvm_init's "movel %d0,smsegs" becomes "jsr kvseg_smsegs_clamp".
|
| Called with d0 = smsegs as computed; stores min(d0, KVSEG_SMSEGS_MAX) and
| returns with the condition codes of that value (kvm_init branches on them
| next: its "addqw #8,%sp" in between leaves them alone).  Only d0 is changed.

	.set	KVSEG_SMSEGS_MAX, 480	| (0x48440000 - 0x40c40000) / 256 KB for 12 MB;
					| patch_kvseg.py rewrites both immediates
					| for its KVSEG_MB

	.text
	.globl	kvseg_smsegs_clamp
kvseg_smsegs_clamp:
	cmpil	&KVSEG_SMSEGS_MAX,%d0
	bles	Lkv_ok
	movel	&KVSEG_SMSEGS_MAX,%d0
Lkv_ok:
	movel	%d0,smsegs
	tstl	%d0
	rts

	.balign 4
