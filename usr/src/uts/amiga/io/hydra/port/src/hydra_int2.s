| hydra_int2.s -- the Hydra's level-2 interrupt handler beside the A2065's.
|
| The level-2 dispatcher (amiga/ml/ttrap.s p2int) jsr's every entry of
| int2_tbl[] = { aciaaintr, jbintr, a2091intr, a3091intr, aenintr, 0 }; the
| table has no spare slot for hydraintr (master.d kernel.c adds it when the
| kernel is built from source).  patch_hydra_int2.py retargets the one
| relocation of the aenintr entry to hydra_int2, which calls aenintr, as
| before, and then hydraintr.  Both are C and declared without parameters;
| p2int's argument (sp@(4), a struct pcb *) is passed on to each all the same,
| and p2int ignores the return value.  %a2 (p2int's table pointer) and every
| other register the C convention keeps are kept by the C callees.
| Assemble: m68k-cbm-sysv4-as -o hydra_int2.o hydra_int2.s

	.text
	.globl	hydra_int2
hydra_int2:
	movel	%sp@(4),%sp@-
	jsr	aenintr
	addql	&4,%sp
	movel	%sp@(4),%sp@-
	jsr	hydraintr
	addql	&4,%sp
	rts
	.align	4
