| parinit_zz9000.s -- io_init hook for the Z3660 RTG driver (local, isoriano 2026-10-16).
| Chained in front of the Z3660 wrapper (amix-z3660rtg DESIGN.md 11.2):
| io_init -> zz9000init -> parinit_z3660 (z3660 glue) -> parinit_orig (stock).
	.text
	.globl	parinit
parinit:
	jsr	zz9000init
	jmp	parinit_z3660
	.balign 4
