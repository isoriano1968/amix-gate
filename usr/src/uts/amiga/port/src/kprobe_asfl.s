| kprobe_asfl.s -- which boot step writes as_freelist?  (2026-11-06, debug)
|
| At 256 MB as_freelist holds 0x200 at main's first as_alloc although mlsetup
| zeroed the BSS.  Each wrapper below runs one boot-once step, then checks
| as_freelist and panics naming the step after which it is no longer zero.
| The caller's return address is parked in a slot (no reentrancy: every one
| of these has exactly one call site and runs once).

	.text
	.globl	kprobe_magic
	.globl	sysseginit
sysseginit:
	movel	%sp@+,kp_ret_sysseginit
	jsr	sysseginit_orig
	movel	as_freelist,%d1
	beqs	kp_ok_sysseginit
	movel	%d1,%sp@-
	pea	kp_s_sysseginit
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_sysseginit:	bras	kp_hang_sysseginit
kp_ok_sysseginit:	movel	kp_ret_sysseginit,%sp@-
	rts
	.globl	kvm_init
kvm_init:
	movel	%sp@+,kp_ret_kvm_init
	jsr	kvm_init_orig
	movel	as_freelist,%d1
	beqs	kp_ok_kvm_init
	movel	%d1,%sp@-
	pea	kp_s_kvm_init
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_kvm_init:	bras	kp_hang_kvm_init
kp_ok_kvm_init:	movel	kp_ret_kvm_init,%sp@-
	rts
	.globl	dispinit
dispinit:
	movel	%sp@+,kp_ret_dispinit
	jsr	dispinit_orig
	movel	as_freelist,%d1
	beqs	kp_ok_dispinit
	movel	%d1,%sp@-
	pea	kp_s_dispinit
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_dispinit:	bras	kp_hang_dispinit
kp_ok_dispinit:	movel	kp_ret_dispinit,%sp@-
	rts
	.globl	p0init
p0init:
	movel	%sp@+,kp_ret_p0init
	jsr	p0init_orig
	movel	as_freelist,%d1
	beqs	kp_ok_p0init
	movel	%d1,%sp@-
	pea	kp_s_p0init
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_p0init:	bras	kp_hang_p0init
kp_ok_p0init:	movel	kp_ret_p0init,%sp@-
	rts
	.globl	startup
startup:
	movel	%sp@+,kp_ret_startup
	jsr	startup_orig
	movel	as_freelist,%d1
	beqs	kp_ok_startup
	movel	%d1,%sp@-
	pea	kp_s_startup
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_startup:	bras	kp_hang_startup
kp_ok_startup:	movel	kp_ret_startup,%sp@-
	rts
	.globl	vfs_mountroot
vfs_mountroot:
	movel	as_freelist,%d1
	beqs	kp_in_vfs_mountroot
	movel	%d1,%sp@-
	pea	kp_s_before_mount
	pea	kp_fmt2
	pea	3
	jsr	cmn_err
kp_hang_before:	bras	kp_hang_before
kp_in_vfs_mountroot:
	movel	%sp@+,kp_ret_vfs_mountroot
	jsr	vfs_mountroot_orig
	movel	as_freelist,%d1
	beqs	kp_ok_vfs_mountroot
	movel	%d1,%sp@-
	pea	kp_s_vfs_mountroot
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_vfs_mountroot:	bras	kp_hang_vfs_mountroot
kp_ok_vfs_mountroot:	movel	kp_ret_vfs_mountroot,%sp@-
	rts
	.globl	swapconf
swapconf:
	movel	%sp@+,kp_ret_swapconf
	jsr	swapconf_orig
	movel	as_freelist,%d1
	beqs	kp_ok_swapconf
	movel	%d1,%sp@-
	pea	kp_s_swapconf
	pea	kp_fmt
	pea	3
	jsr	cmn_err
kp_hang_swapconf:	bras	kp_hang_swapconf
kp_ok_swapconf:	movel	kp_ret_swapconf,%sp@-
	rts
| biowait(bp): after every wait, check; on the first non-zero, print the buf
| header longs 0..12 (flags, links, dev, count, addr, blkno ...).
	.globl	biowait
biowait:
	linkw	%fp,&0
	movel	%fp@(8),%sp@-
	jsr	biowait_orig
	addqw	&4,%sp
	movel	as_freelist,%d1
	beqs	kp_bw_ok
	moveal	%fp@(8),%a1
	moveq	&12,%d1
kp_bw_push:
	movel	%a1@(0,%d1:l:4),%sp@-
	dbf	%d1,kp_bw_push
	movel	%a1,%sp@-
	movel	as_freelist,%sp@-
	pea	kp_fmt3
	pea	3
	jsr	cmn_err
kp_bw_hang:	bras	kp_bw_hang
kp_bw_ok:
	unlk	%fp
	rts

kp_fmt:	.asciz	"kprobe: %s, as_freelist %x"
kp_fmt2:	.asciz	"kprobe: %s, as_freelist %x"
kp_fmt3:	.asciz	"kprobe: after biowait as_freelist %x bp %x: %x %x %x %x %x %x %x %x %x %x %x %x %x"
kp_s_before_mount:	.asciz	"before vfs_mountroot (drivers)"
kp_s_sysseginit:	.asciz	"sysseginit"
kp_s_kvm_init:	.asciz	"kvm_init"
kp_s_dispinit:	.asciz	"dispinit"
kp_s_p0init:	.asciz	"p0init"
kp_s_startup:	.asciz	"startup"
kp_s_vfs_mountroot:	.asciz	"vfs_mountroot"
kp_s_swapconf:	.asciz	"swapconf"
	.even
kprobe_magic:	.ascii	"KPR!"
	.data
kp_ret_sysseginit:	.long	0
kp_ret_kvm_init:	.long	0
kp_ret_dispinit:	.long	0
kp_ret_p0init:	.long	0
kp_ret_startup:	.long	0
kp_ret_vfs_mountroot:	.long	0
kp_ret_swapconf:	.long	0
