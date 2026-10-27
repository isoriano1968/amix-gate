| parinit_hydra.s -- register the Hydra Systems AmigaNet driver (hydra.c) in a
| 68040/68060 kernel built by relink-040-hydra.sh.
|
| The stock tables cannot grow inside a linked image, so the driver is wired
| in at run time, first thing in parinit (the same way z3660_glue040.s puts
| z3660ethinfo into cdevsw[51]):
|   * hydrainit() -- finds the boards (bootinfo autocon, 0x08680001);
|   * cdevsw[47].d_str = &hydrainfo -- the slot AMIX trees with this driver
|     use (master.d/kernel.c: 47=hydra); cdevsw is 52-byte entries, d_str at
|     +44; relink-040-hydra.sh asserts the slot is empty first;
|   * int2_tbl[4] (aenintr, the A2065) -> hydra_int2, which calls aenintr and
|     then hydraintr: level 2 is shared, every handler checks its own board.
|     relink-040-hydra.sh asserts that int2_tbl[4] is aenintr.
| Then the previous parinit (the RTG/Z3660 chain) runs.

	.text
	.globl	parinit
parinit:
	jsr	hydrainit
	movel	&hydrainfo,cdevsw+2488		| cdevsw[47].d_str  (47*52 + 44)
	movel	&hydra_int2,int2_tbl+16		| int2_tbl[4]
	jmp	parinit_prev

	.globl	hydra_int2
hydra_int2:
	jsr	aenintr
	jmp	hydraintr

	.balign 4
