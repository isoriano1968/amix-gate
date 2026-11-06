| ksegtbl.s -- kseg()'s page-count table, sized for the whole index range.
| (2026-11-06, the 256 MB as_freelist bug)
|
| kseg() (.text+0xa8d1a) and unkseg() (.text+0xa8e26) keep the size of each
| kseg allocation in ksegtbl[((va - syssegs) >> 17) & 0x1fff], one byte per
| 128 KB of kvseg.  The stock table is 32 bytes: enough for the stock 4 MB
| kvseg and nothing more.  This port's kvseg is 12 to 32 MB (KVSEG_MB), so
| kseg wrote past the end, into anonmap_freelist, segvn_freelist, npagesteal,
| anon_lock and as_freelist (ksegtbl + 0x40).  At 256 MB cinit's clist
| allocations land at index 66 and 70: as_freelist = 0x00000200, init's
| address space was built in the chip RAM vector table, then in the ROM, and
| exec of init took a bus error in as_addseg.  Smaller RAM hit the variables
| before as_freelist instead, silently.
|
| This table covers the full 13-bit index (8192 bytes = 1 GB of kvseg); the
| stock symbol is weakened, so kseg and unkseg bind to this one unchanged.
	.data
	.globl	ksegtbl
ksegtbl:
	.space	8192
