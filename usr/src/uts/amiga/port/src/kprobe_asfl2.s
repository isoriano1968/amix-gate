| kprobe_asfl2.s -- generated 2026-11-06: as_freelist check after each step
| between startup and vfs_mountroot (main: inituname..setupclock, io_init,
| init_tbl).  Panics naming the first step after which it is non-zero.
	.text
	.globl	inituname
inituname:
	movel	%sp@+,kp_ret_inituname
	jsr	inituname_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_inituname
	movel	%d1,%sp@-
	pea	kp_s_inituname
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_inituname:	bras	kp_hang_inituname
kp_ok_inituname:	movel	kp_ret_inituname,%sp@-
	rts
	.globl	clkstart
clkstart:
	movel	%sp@+,kp_ret_clkstart
	jsr	clkstart_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_clkstart
	movel	%d1,%sp@-
	pea	kp_s_clkstart
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_clkstart:	bras	kp_hang_clkstart
kp_ok_clkstart:	movel	kp_ret_clkstart,%sp@-
	rts
	.globl	cred_init
cred_init:
	movel	%sp@+,kp_ret_cred_init
	jsr	cred_init_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_cred_init
	movel	%d1,%sp@-
	pea	kp_s_cred_init
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_cred_init:	bras	kp_hang_cred_init
kp_ok_cred_init:	movel	kp_ret_cred_init,%sp@-
	rts
	.globl	dnlc_init
dnlc_init:
	movel	%sp@+,kp_ret_dnlc_init
	jsr	dnlc_init_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_dnlc_init
	movel	%d1,%sp@-
	pea	kp_s_dnlc_init
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_dnlc_init:	bras	kp_hang_dnlc_init
kp_ok_dnlc_init:	movel	kp_ret_dnlc_init,%sp@-
	rts
	.globl	parinit
parinit:
	movel	%sp@+,kp_ret_parinit
	jsr	parinit_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_parinit
	movel	%d1,%sp@-
	pea	kp_s_parinit
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_parinit:	bras	kp_hang_parinit
kp_ok_parinit:	movel	kp_ret_parinit,%sp@-
	rts
	.globl	fpuinit
fpuinit:
	movel	%sp@+,kp_ret_fpuinit
	jsr	fpuinit_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_fpuinit
	movel	%d1,%sp@-
	pea	kp_s_fpuinit
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_fpuinit:	bras	kp_hang_fpuinit
kp_ok_fpuinit:	movel	kp_ret_fpuinit,%sp@-
	rts
	.globl	cinit
cinit:
	movel	%sp@+,kp_ret_cinit
	jsr	cinit_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_cinit
	movel	%d1,%sp@-
	pea	kp_s_cinit
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_cinit:	bras	kp_hang_cinit
kp_ok_cinit:	movel	kp_ret_cinit,%sp@-
	rts
	.globl	binit
binit:
	movel	%sp@+,kp_ret_binit
	jsr	binit_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_binit
	movel	%d1,%sp@-
	pea	kp_s_binit
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_binit:	bras	kp_hang_binit
kp_ok_binit:	movel	kp_ret_binit,%sp@-
	rts
	.globl	inoinit
inoinit:
	movel	%sp@+,kp_ret_inoinit
	jsr	inoinit_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_inoinit
	movel	%d1,%sp@-
	pea	kp_s_inoinit
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_inoinit:	bras	kp_hang_inoinit
kp_ok_inoinit:	movel	kp_ret_inoinit,%sp@-
	rts
	.globl	vfsinit
vfsinit:
	movel	%sp@+,kp_ret_vfsinit
	jsr	vfsinit_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_vfsinit
	movel	%d1,%sp@-
	pea	kp_s_vfsinit
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_vfsinit:	bras	kp_hang_vfsinit
kp_ok_vfsinit:	movel	kp_ret_vfsinit,%sp@-
	rts
	.globl	finit
finit:
	movel	%sp@+,kp_ret_finit
	jsr	finit_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_finit
	movel	%d1,%sp@-
	pea	kp_s_finit
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_finit:	bras	kp_hang_finit
kp_ok_finit:	movel	kp_ret_finit,%sp@-
	rts
	.globl	strinit
strinit:
	movel	%sp@+,kp_ret_strinit
	jsr	strinit_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_strinit
	movel	%d1,%sp@-
	pea	kp_s_strinit
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_strinit:	bras	kp_hang_strinit
kp_ok_strinit:	movel	kp_ret_strinit,%sp@-
	rts
	.globl	msginit
msginit:
	movel	%sp@+,kp_ret_msginit
	jsr	msginit_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_msginit
	movel	%d1,%sp@-
	pea	kp_s_msginit
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_msginit:	bras	kp_hang_msginit
kp_ok_msginit:	movel	kp_ret_msginit,%sp@-
	rts
	.globl	seminit
seminit:
	movel	%sp@+,kp_ret_seminit
	jsr	seminit_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_seminit
	movel	%d1,%sp@-
	pea	kp_s_seminit
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_seminit:	bras	kp_hang_seminit
kp_ok_seminit:	movel	kp_ret_seminit,%sp@-
	rts
	.globl	sadinit
sadinit:
	movel	%sp@+,kp_ret_sadinit
	jsr	sadinit_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_sadinit
	movel	%d1,%sp@-
	pea	kp_s_sadinit
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_sadinit:	bras	kp_hang_sadinit
kp_ok_sadinit:	movel	kp_ret_sadinit,%sp@-
	rts
	.globl	tclinit
tclinit:
	movel	%sp@+,kp_ret_tclinit
	jsr	tclinit_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_tclinit
	movel	%d1,%sp@-
	pea	kp_s_tclinit
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_tclinit:	bras	kp_hang_tclinit
kp_ok_tclinit:	movel	kp_ret_tclinit,%sp@-
	rts
	.globl	tcoinit
tcoinit:
	movel	%sp@+,kp_ret_tcoinit
	jsr	tcoinit_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_tcoinit
	movel	%d1,%sp@-
	pea	kp_s_tcoinit
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_tcoinit:	bras	kp_hang_tcoinit
kp_ok_tcoinit:	movel	kp_ret_tcoinit,%sp@-
	rts
	.globl	tcooinit
tcooinit:
	movel	%sp@+,kp_ret_tcooinit
	jsr	tcooinit_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_tcooinit
	movel	%d1,%sp@-
	pea	kp_s_tcooinit
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_tcooinit:	bras	kp_hang_tcooinit
kp_ok_tcooinit:	movel	kp_ret_tcooinit,%sp@-
	rts
	.globl	setupclock
setupclock:
	movel	%sp@+,kp_ret_setupclock
	jsr	setupclock_kpo
	movel	as_freelist,%d1
	beqs	kp_ok_setupclock
	movel	%d1,%sp@-
	pea	kp_s_setupclock
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_setupclock:	bras	kp_hang_setupclock
kp_ok_setupclock:	movel	kp_ret_setupclock,%sp@-
	rts
kp_fmt:	.asciz	"kprobe2: after %s, as_freelist %x"
kp_s_inituname:	.asciz	"inituname"
kp_s_clkstart:	.asciz	"clkstart"
kp_s_cred_init:	.asciz	"cred_init"
kp_s_dnlc_init:	.asciz	"dnlc_init"
kp_s_parinit:	.asciz	"parinit"
kp_s_fpuinit:	.asciz	"fpuinit"
kp_s_cinit:	.asciz	"cinit"
kp_s_binit:	.asciz	"binit"
kp_s_inoinit:	.asciz	"inoinit"
kp_s_vfsinit:	.asciz	"vfsinit"
kp_s_finit:	.asciz	"finit"
kp_s_strinit:	.asciz	"strinit"
kp_s_msginit:	.asciz	"msginit"
kp_s_seminit:	.asciz	"seminit"
kp_s_sadinit:	.asciz	"sadinit"
kp_s_tclinit:	.asciz	"tclinit"
kp_s_tcoinit:	.asciz	"tcoinit"
kp_s_tcooinit:	.asciz	"tcooinit"
kp_s_setupclock:	.asciz	"setupclock"
	.even
	.data
kp_ret_inituname:	.long	0
kp_ret_clkstart:	.long	0
kp_ret_cred_init:	.long	0
kp_ret_dnlc_init:	.long	0
kp_ret_parinit:	.long	0
kp_ret_fpuinit:	.long	0
kp_ret_cinit:	.long	0
kp_ret_binit:	.long	0
kp_ret_inoinit:	.long	0
kp_ret_vfsinit:	.long	0
kp_ret_finit:	.long	0
kp_ret_strinit:	.long	0
kp_ret_msginit:	.long	0
kp_ret_seminit:	.long	0
kp_ret_sadinit:	.long	0
kp_ret_tclinit:	.long	0
kp_ret_tcoinit:	.long	0
kp_ret_tcooinit:	.long	0
kp_ret_setupclock:	.long	0
