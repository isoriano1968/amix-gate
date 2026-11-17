/*
 * zz9000eth.c -- MNT ZZ9000 onboard ethernet for AMIX (SVR4; stock 68030
 * kernels and the 68040/68060 port kernels).
 *
 * Derived from Jussi Alanärä's z3660eth (io/z3660net, MIT; eth 1.0.0-7 with
 * the AMIX port's patches), the STREAMS/DLPI driver of the Z3660's ethernet.
 * The Z3660 firmware descends from the ZZ9000's, so the protocol is the same:
 * the firmware owns the GEM and moves whole frames through a small register
 * mailbox and shared frame windows, and this driver presents that to AMIX as
 * a DLPI Style-1 connectionless ethernet provider (/dev/zz0, interface zz0).
 * Everything above the hardware -- STREAMS open/close, DLPI, the RX
 * demultiplexer and backpressure, the poll, the 040/060 cache gate -- is
 * z3660eth's, renamed.  The board layer is the ZZ9000's (zz9000eth.h states
 * the interface and its sources):
 *   ZZ_CONFIG(0x04)   W 1 enable / 24 ack ETH / 0 disable; R pending bits
 *   ZZ_ETH_TX(0x80)   W byte length = send the frame at +0x8000 (synchronous,
 *                     up to ~1 ms); R = result << 8
 *   ZZ_ETH_RX(0x82)   W any = the frame in the RX window is consumed
 *   ZZ_ETH_MAC_HI/HI2/LO (0x84/0x86/0x88)  the firmware-owned station MAC
 *   RX window @ board+0x2000: the firmware's current backlog slot, a 4-byte
 *   [len][serial] header and the frame; TX frame @ board+0x8000.
 *
 * Detection: AutoConfig only -- MNT (0x6d6e) product 3 (Zorro II) or 4
 * (Zorro III).  No raw address is ever read: without a ZZ9000 the driver
 * touches nothing and says nothing; the first open returns ENXIO.  The board
 * is the zz9000 graphics driver's too (io/zz9000): this driver uses only the
 * registers above and the windows 0x2000..0x9FFF, the graphics driver none of
 * them, and neither takes an interrupt.
 *
 * RX delivery: the firmware signals frames on INT6 (EXTER) when REG_ZZ_CONFIG
 * enables it, and AMIX has no level-6 driver hook, so -- as z3660eth -- the
 * interrupt is left OFF (CONFIG 0) and the backlog is drained from a
 * clock-level timeout() poll callout.  zz9000ethintr() is the handler for a
 * later interrupt chain, not wired.  TX is synchronous and needs no interrupt.
 *
 * Caches.  030 line: the windows are cacheable as every Zorro II board is to
 * AMIX, and the 030 data cache is effectively off in every validated config
 * (z3660eth's proven no-flush behaviour).  040/060 line: a Zorro II board lies
 * below VSECT1, where DTT0 = 0x003fc060 makes it cache-inhibited (the "free
 * ride" z3660eth asserts, kept here); a Zorro III board above it is mapped
 * noncacheable through dev_kvmap (-DZZ9000ETH_KVMAP).
 */
#include "sys/types.h"
#include "sys/param.h"
#include "sys/signal.h"
#include "sys/errno.h"
#include "sys/psw.h"
#include "sys/pcb.h"
#include "sys/user.h"
#include "sys/sysmacros.h"	/* makedevice, getminor, getmajor */
#include "sys/immu.h"		/* PG_V, phystopfn, paddr_t */
#include "sys/stream.h"
#include "sys/stropts.h"
#include "sys/queue.h"
#include "sys/sbd.h"
#include "sys/debug.h"
#include "sys/systm.h"
#include "sys/cred.h"
#include "sys/dlpi.h"
#include "sys/ddi.h"
#include "sys/cmn_err.h"
#include "sys/socket.h"
#include "sys/sockio.h"
#include "net/if.h"
#include "zz9000eth.h"

extern int	autocon();
extern caddr_t	sptalloc();
extern void	sptfree();
#ifdef ZZ9000ETH_KVMAP
/*
 * 040/060 line (make-kit-040.sh defines ZZ9000ETH_KVMAP and links the port's
 * src/devkvmap040.s, renamed): a board at or above VSECT1 -- a Zorro III
 * ZZ9000 -- is mapped with explicit noncacheable classes
 * (dev_kvmap, as the VA2000 driver does) instead of sptalloc's writethrough-
 * cacheable leaves.  Nothing in those windows is ever cached, so the line ops
 * stay disarmed there and are not needed.  cputype: the port's CPU class.
 */
extern caddr_t	dev_kvmap();
extern void	dev_kvunmap();
/* the port's cputype is a long (src/cputype060.s); sys/systm.h still declares
 * the SVR4 short, so it is reached under its own name */
extern long	zz9000eth_cputype __asm__("cputype");
#define ZZ_CM_NCS	0x40		/* registers: noncacheable, serialized */
#define ZZ_CM_NC	0x60		/* frame window: noncacheable */
#define ZZ_KVMAPPED	2		/* spt_mapped: the dev_kvmap arm */
#endif
extern int	timeout();
extern void	bcopy(), bzero();

#ifndef min
#define min(a,b)	((a) < (b) ? (a) : (b))
#endif

extern char *panicstr;

void zz9000eth_autoconfig();
void zz9000ethinit();
void zz9000ethintr();
static void zz9000eth_poll();
static void zz9000eth_drain();
static void zz9000eth_tx_drain();
static void toss_packet_up_stream();
static void zz9000eth_rx_want();
static int  zz9000ethopen(), zz9000ethclose(), zz9000ethwput(), zz9000ethwsrv();
static int  zz9000ethrsrv();
static void zz9000ethioctl(), zz9000eth_ioc_nak();
static int  zz9000eth_ioc_arg();
static int  zz9000ethproto(), zz9000ethxmit();
static int  zz9000eth_map(), zz9000eth_initialize();
static void zz9000eth_unmap();
static int  zz9000eth_absent();
#define ZZ_ABS_QUIET	0	/* zz9000eth_absent: no board, no console line */
#define ZZ_ABS_MAC	1	/* append the MAC register words */
#define ZZ_ABS_SAY	2	/* a board is there: one console line */

static struct module_info zz9000eth_minfo =
{
    0x7A7A, "zz9000eth", ZZ9000ETH_MIN, ZZ9000ETH_MAX, ZZ9000ETH_HIWAT, ZZ9000ETH_LOWAT,
};

static struct qinit zz9000ethrinit =
{
    NULL, zz9000ethrsrv, zz9000ethopen, zz9000ethclose, NULL, &zz9000eth_minfo, NULL,
};

static struct qinit zz9000ethwinit =
{
    zz9000ethwput, zz9000ethwsrv, NULL, NULL, NULL, &zz9000eth_minfo, NULL,
};

struct streamtab zz9000ethinfo =		/* goes in cdevsw[].d_str */
{
    &zz9000ethrinit, &zz9000ethwinit, NULL, NULL,
};

extern struct ifstats *ifstats;	/* the module uses if_attach (sys/amixddi.h) */

static zz9000eth_board_t zz9000eth_board[ZZ9000ETH_MAXBOARDS];
static int zz9000eth_number_of_boards;
static int zz9000eth_autoconfigured;

/* diagnostics (read via /dev/kmem on real HW) */
unsigned long zz9000eth_board_phys, zz9000eth_machi, zz9000eth_maclo;
unsigned char zz9000eth_present;
unsigned char zz9000eth_direct_map;	/* 1 = section-0 identity, 0 = sptalloc'd */

/*
 * Since 2026-09-27 a failed probe is remembered per board (info->map_verdict)
 * so later opens neither map nor probe again, and it prints ONE console line.
 * zz9000eth_reprobe is the re-probe knob: poke it nonzero through /dev/kmem and
 * the next open forgets one cached verdict and probes afresh (the knob clears
 * itself).  For a firmware whose ethernet comes up after the first open; there
 * is no Zorro hot-plug.  The counters are diagnostics for /dev/kmem readers.
 */
long zz9000eth_reprobe;
unsigned long zz9000eth_map_fails;	/* probes that ended in a failure verdict */
unsigned long zz9000eth_spt_freed;	/* kernel-VA pages returned by sptfree() */

/*
 * RX backpressure counters (zz9000eth_drain), kmem-readable; the status struct
 * is the GET_STATUS ABI and stays as it is.
 *   rx_holds        drains that stopped with a frame left in the ring
 *   rx_resumed      drains run by the read service procedure (back-enabled)
 *   rx_force_ticks  frames released past ZZ9000ETH_RX_HOLD_TICKS
 * A forced frame still goes to every stream that can take it; each stream that
 * cannot is counted in couldnt_put, which has no other source now.
 */
unsigned long zz9000eth_rx_holds, zz9000eth_rx_resumed;
unsigned long zz9000eth_rx_force_ticks;
/* frames not ahead of the last serial at a strobe-free stop (the caught-up
 * slot 0, see zz9000eth_drain), and serial gaps (tail drops), kmem-readable */
unsigned long zz9000eth_rx_stale, zz9000eth_rx_skipped;


/*
 * The tail of a probe-failure console line.  The static driver probes at open
 * and again later; the module probes once, in _load, and a failure there means
 * the module is not kept (C3-6 section 1.1: a console line per refusal).
 */
#define ZZ9000ETH_FAILS	"opens fail errno"
#define ZZ9000ETH_RETRY	"will retry"

static unsigned char zz9000eth_bcast[ETH_ADDRESS_SIZE] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

/*
 * The board's interrupt control and the RX strobe.  z3660eth issues every
 * board access behind an arming read (the Z3660 FPGA's ZS-05 address-capture
 * defect); the ZZ9000's Zorro logic latches each address on its own, so the
 * accesses here are plain 16-bit ones (zz9000eth.h ZZ_RD16/ZZ_WR16).
 */
#define ZZ9000ETH_CONFIG(r_, x_)	ZZ_WR16((r_), ZZ_CONFIG, (x_))
#define ZZ9000ETH_RX_STROBE(r_)	ZZ_WR16((r_), ZZ_ETH_RX, 1)

#ifdef ZZ9000ETH_KVMAP
/*
 * The 040/060 cache gate and line maintenance below are z3660eth's, unchanged
 * but for the names, and built only into the 040/060 kit (-DZZ9000ETH_KVMAP):
 * z3660eth links one object into every line, zz9000eth has a kit per line,
 * and the 030 kit's check refuses 040/060 opcodes even unreachable ones.
 * The 030 build has the no-op forms after the #else.
 */
/* ----------------------------------------------------------------------------
 * BLIZZARD F3-M0 -- the 040/060 gate, and the free ride asserted instead of
 * silently depended on.  Full argument in z3660net docs/BLIZZARD-F3.md.
 *
 * THE GATE.  This file is compiled ONCE, with -m68020, and the one object is
 * linked into kernels that run on an emulated 030, an emulated 040 and (from
 * BLIZZARD F4) a real 68060.  There is no compile-time discriminator, and
 * `cputype' may NOT be referenced -- the stock kernel has no such symbol, so an
 * extern to it would break the nm -u clean gate on the 030 line.  So the class
 * is a driver-owned global, default OFF, poked through /dev/kmem exactly like
 * the port lane's hg_on / i40_on / hat_cm_ram.  With it 0 no 040/060 opcode in
 * this file is reachable, which is every shipping image today.
 *
 * THE FREE RIDE.  On the 040/060 line pstart040.s loads DTT0 = 0x003fc060: the
 * whole low 1 GB is cache-inhibited for DATA, so on the direct-map arm every
 * window this driver touches is CI without the driver knowing it.  That is a
 * kernel constant no code here names, and the port lane has a written plan to
 * narrow it (DTT0-NARROWING-SPEC.md stage N1 keeps only the low 16 MiB, which
 * is far below this board's 0x10000000).  Check it at attach and say so out
 * loud when it stops being true, rather than discovering it as torn frames.
 *
 * THE SAME REGISTER ALSO GIVES US THE ADDRESSES.  CPUSHL/CINVL select a line by
 * PHYSICAL address and this driver holds kernel VAs; a TTR match is what makes
 * a logical address a physical one here.  So the line ops are armed only when
 * ALL FOUR hold: the gate is on, the direct-map arm was taken, phystokv() is
 * really the identity over both mapped regions, and a TTR covers both of them
 * cache-inhibited.  On the sptalloc arm the VA has no relation to the board's
 * physical address, and a CINVL on it would discard an unrelated dirty line
 * belonging to some other subsystem -- a fix that destroys data is worse than
 * no fix, so maintenance DISARMS there rather than guessing.
 * ------------------------------------------------------------------------- */
long zz9000eth_cache;		/* 0 = off (default), 40 = 68040 DC, 60 = 68060 DC */
long zz9000eth_ci_enforce;	/* 0 = warn on a failed free-ride check, else refuse */

/* attach-time capture + verdict, all kmem-readable (z3660net docs/BLIZZARD-F3.md 7) */
unsigned long zz9000eth_dtt0, zz9000eth_dtt1, zz9000eth_cacr;
unsigned long zz9000eth_ci_ok;		/* 0 = not checked, 1 = verified, 2 = FAILED */
unsigned long zz9000eth_maint_on;	/* 1 = the four conjuncts hold; line ops armed */
unsigned long zz9000eth_sptalloc_unsafe;	/* sptalloc arm taken with a live data cache */

/*
 * 040/060 control-register reads, as raw .word so the -m68020 assembler accepts
 * them, with the destination pinned by an explicit register variable.  Verified
 * against this toolchain (gcc 2.7.2.3 / GNU as 2.8.1) by assembling the mnemonic
 * and disassembling the word, both directions:
 *   4e7a 0006 = movec %dtt0,%d0   4e7a 0007 = movec %dtt1,%d0
 *   4e7a 0002 = movec %cacr,%d0
 * Every caller is gated on zz9000eth_cache != 0, so none of this is reachable on
 * the 68030 line, where these words would fault.
 */
static unsigned long
zz9000eth_rd_dtt0()
{
    register unsigned long v __asm__("d0");

    __asm__ __volatile__(".word 0x4e7a,0x0006" : "=d" (v));
    return v;
}

static unsigned long
zz9000eth_rd_dtt1()
{
    register unsigned long v __asm__("d0");

    __asm__ __volatile__(".word 0x4e7a,0x0007" : "=d" (v));
    return v;
}

static unsigned long
zz9000eth_rd_cacr()
{
    register unsigned long v __asm__("d0");

    __asm__ __volatile__(".word 0x4e7a,0x0002" : "=d" (v));
    return v;
}

/*
 * Classify one transparent-translation register against one address, for
 * SUPERVISOR DATA accesses:
 *   0 = does not cover this address
 *   1 = covers it, cache-inhibited	(the free ride)
 *   2 = covers it, CACHED		(the free ride is gone)
 *
 * TTR layout (040 and 060 alike; the decode is confirmed by pstart040.s's own
 * comments, which call ITT0 0x003fc000 "WT-cacheable" and DTT1 0x807fa060
 * "cache-inhib"):
 *   31-24 base   23-16 mask   15 E   14-13 S   6-5 CM
 *   (CM: 0 WT, 1 copyback, 2 CI-serialized, 3 CI-nonserialized)
 */
static int
zz9000eth_ttr_class(t, a)
unsigned long t, a;
{
    unsigned long base, mask, s, cm;

    if ((t & 0x8000) == 0)		/* E: a disabled TTR covers nothing */
	return 0;
    s = (t >> 13) & 3;			/* 0 = user only, 1 = supervisor, 2/3 = both */
    if (s == 0)
	return 0;
    base = (t >> 24) & 0xFF;
    mask = (t >> 16) & 0xFF;
    if ((((a >> 24) & 0xFF) & ~mask) != (base & ~mask))
	return 0;
    cm = (t >> 5) & 3;
    return (cm >= 2) ? 1 : 2;
}

/*
 * Check one mapped region, exhaustively.  A TTR's match function depends only on
 * address bits 31-24, so ONE probe per 16 MB block covers the whole block, and
 * stepping the blocks covers the region -- which endpoint sampling would not,
 * because a mask with non-adjacent bits covers a NON-CONTIGUOUS set of blocks
 * and this driver's frame region sits 0x07ED0000 above its register region.
 * Returns 1 if any block is not reached cache-inhibited.
 */
static int
zz9000eth_ci_span(lo, hi)
unsigned long lo, hi;			/* lo inclusive, hi exclusive */
{
    unsigned long a, last;
    int bad = 0, c0, c1;

    a = lo;
    last = (hi - 1) & ~0x00FFFFFFL;
    for (;;) {
	c0 = zz9000eth_ttr_class(zz9000eth_dtt0, a);
	c1 = zz9000eth_ttr_class(zz9000eth_dtt1, a);
	if (c0 == 2 || c1 == 2 || (c0 == 0 && c1 == 0)) {
	    bad = 1;
	    cmn_err(CE_WARN,
		"zz9000eth: VA 0x%x is NOT cache-inhibited (dtt0 0x%x dtt1 0x%x)",
		a, zz9000eth_dtt0, zz9000eth_dtt1);
	}
	if ((a & ~0x00FFFFFFL) >= last)
	    break;
	a = (a & ~0x00FFFFFFL) + 0x01000000L;
    }
    return bad;
}

/*
 * F3-M0.  Run once per successful map, before the first register read, so a
 * machine whose mailbox would answer out of a stale cache line says so before
 * anything is believed.  Returns 0 to continue, ENXIO to refuse the attach --
 * only when zz9000eth_ci_enforce is set, because a first-silicon boot that
 * refuses its own network device tells you nothing.
 */
int
zz9000eth_ci_check(board_index)
int board_index;
{
    zz9000eth_info_t *info = &zz9000eth_board[board_index].zz9000eth_info;
    unsigned long base = (unsigned long)info->board_base;
    int bad;

    zz9000eth_ci_ok = 0;
    zz9000eth_maint_on = 0;
    if (zz9000eth_cache == 0)		/* emulated CPU: nothing to check, nothing to do */
	return 0;

    zz9000eth_dtt0 = zz9000eth_rd_dtt0();
    zz9000eth_dtt1 = zz9000eth_rd_dtt1();
    zz9000eth_cacr = zz9000eth_rd_cacr();
#ifdef ZZ9000ETH_KVMAP
    if (zz9000eth_direct_map == 0 && info->spt_mapped == ZZ_KVMAPPED) {
	zz9000eth_ci_ok = 1;		/* noncacheable leaves, PFNs asserted */
	return 0;			/* nothing cached: no line ops needed */
    }
#endif

    bad  = zz9000eth_ci_span(base, base + ZZ_REGS_BYTES);
    bad |= zz9000eth_ci_span(base + ZZ_FRAME_OFF, base + ZZ_WINDOW_TOP);

    if (zz9000eth_direct_map == 0) {
	zz9000eth_sptalloc_unsafe++;
	bad = 1;
	cmn_err(CE_WARN,
	    "zz9000eth: sptalloc mapping arm is unsupported with a live data cache");
    } else if ((unsigned long)info->regs != base ||
	       (unsigned long)info->frame != base + ZZ_FRAME_OFF) {
	bad = 1;
	cmn_err(CE_WARN,
	    "zz9000eth: phystokv is not the identity (base 0x%x regs 0x%x frame 0x%x)",
	    base, (unsigned long)info->regs, (unsigned long)info->frame);
    }

    if (bad == 0) {
	zz9000eth_ci_ok = 1;
	zz9000eth_maint_on = 1;
	return 0;
    }
    zz9000eth_ci_ok = 2;
    cmn_err(CE_WARN, "zz9000eth: cache-inhibit assertion FAILED (cache=%d cacr 0x%x)",
	(int)zz9000eth_cache, zz9000eth_cacr);
    return zz9000eth_ci_enforce ? ENXIO : 0;
}

/* ----------------------------------------------------------------------------
 * BLIZZARD F3-M2 -- 68040/68060 data-cache maintenance for the shared eth DDR
 * windows.  Replaces a block that was #ifdef'd behind a -D nobody passed and was
 * wrong four ways when read (z3660net docs/BLIZZARD-F3.md 3).  No #ifdef now: compiled in
 * always, gated at run time -- a -D nobody passes is not a switch, it is a way
 * to keep code from ever being reviewed.
 *
 * Line ops as raw .word so the -m68020 assembler accepts them, with the address
 * pinned in %a0 by an explicit register variable.  Encodings measured against
 * this toolchain in both directions (as -m68040 -> word, objdump -> mnemonic):
 *   f468 = cpushl dc,%a0@	f448 = cinvl dc,%a0@	4e71 = nop
 *
 * PUSH AND INVAL ARE NOT THE SAME OPERATION.  They used to be: both expanded to
 * cpushl.  In general that writes stale CPU bytes back over fresh device bytes;
 * in THIS driver it cannot, because the RX ring is device-write-only.  What it
 * does do here is worse for being quiet: on the 68060, CPUSHL's invalidation is
 * conditional on CACR.DPI -- clear pushes and invalidates, SET pushes and leaves
 * the line VALID -- so with DPI set a push-as-invalidate does not invalidate at
 * all, the next drain re-reads the same line, and the driver re-delivers a stale
 * frame or misses a serial.  Hence two ops, and every push is followed by an
 * explicit CINVL (harmless on the 040) so one sequence is right on both parts
 * with no CPU-class branch.  CACR is captured at attach so a set DPI is visible
 * rather than inferred.
 *
 * PUSH BEFORE THE CPU WRITES, NEVER INVALIDATE AFTER.  CINVL DISCARDS a dirty
 * line without writing it back, and these ranges round outward to 16-byte lines
 * covering bytes the driver never touched.  The rounding is safe here for a
 * reason derived per window, not inherited: the RX ring is written only by the
 * firmware, so no line covering it can be dirty and an invalidate can discard
 * nothing; the TX window is written only by the CPU, so it takes a PUSH, which
 * writes those neighbours back before invalidating them.  BOTH HALVES OF THAT
 * ARGUMENT DIE if a future zero-copy RX ever writes into a slot.
 * ------------------------------------------------------------------------- */
unsigned long zz9000eth_push_n, zz9000eth_inv_n;		/* line-op invocations   */
unsigned long zz9000eth_push_bytes, zz9000eth_inv_bytes;	/* bytes, pre-rounding   */
unsigned long zz9000eth_sync_n;				/* barriers executed     */
unsigned long zz9000eth_range_ovf;			/* impossible range refused */

#define ZZ9000ETH_CPUSHL(p) __asm__ __volatile__(".word 0xf468" : : "a" (p) : "memory")
#define ZZ9000ETH_CINVL(p)  __asm__ __volatile__(".word 0xf448" : : "a" (p) : "memory")
#define ZZ9000ETH_NOP()	   __asm__ __volatile__(".word 0x4e71" : : : "memory")
#define ZZ9000ETH_LINEPTR   register unsigned char *p __asm__("a0")

/*
 * Push-and-invalidate every line covering [va, va+len).  `va' is a kernel VA
 * that has been PROVEN equal to the physical address (zz9000eth_ci_check), which
 * is what the line ops select by; zz9000eth_maint_on is 0 whenever that proof is
 * unavailable, so nothing here ever fires at an address it did not verify.
 */
static void
zz9000eth_push(va, len)
unsigned long va, len;
{
    ZZ9000ETH_LINEPTR;
    unsigned long end;

    if (zz9000eth_cache == 0 || zz9000eth_maint_on == 0 || len == 0)
	return;
    if (va + len < va) {			/* wrap: refuse rather than loop wild */
	zz9000eth_range_ovf++;
	return;
    }
    p = (unsigned char *)(va & ~15L);
    end = (va + len + 15) & ~15L;
    while ((unsigned long)p < end) {
	ZZ9000ETH_CPUSHL(p);
	ZZ9000ETH_CINVL(p);
	p += 16;
    }
    zz9000eth_push_n++;
    zz9000eth_push_bytes += len;
}

/*
 * Invalidate every line covering [va, va+len) -- RX ring only, where nothing the
 * CPU wrote can live (see the block comment above).
 */
static void
zz9000eth_inval(va, len)
unsigned long va, len;
{
    ZZ9000ETH_LINEPTR;
    unsigned long end;

    if (zz9000eth_cache == 0 || zz9000eth_maint_on == 0 || len == 0)
	return;
    if (va + len < va) {
	zz9000eth_range_ovf++;
	return;
    }
    p = (unsigned char *)(va & ~15L);
    end = (va + len + 15) & ~15L;
    while ((unsigned long)p < end) {
	ZZ9000ETH_CINVL(p);
	p += 16;
    }
    zz9000eth_inv_n++;
    zz9000eth_inv_bytes += len;
}

/*
 * The store/load ordering barrier.  DTT0 is CM=11, cache-inhibited NONSERIALIZED,
 * so a doorbell write and the load that follows it are ordered only because the
 * 68060 store buffer is currently OFF (CACR bit 29; the port lane lists turning
 * it on as candidate 1).  The pair that matters is a LOAD passing a buffered
 * STORE to the same register: on TX that returns the PREVIOUS frame's result and
 * mis-scores this one; on RX it returns the pre-strobe cursor and ends the drain
 * a tick early.  Placed now, gated, so it is in the code before the knob arms the
 * hazard -- it costs nothing today.
 *
 * NOT gated on zz9000eth_maint_on: it takes no address, so the identity proof the
 * line ops need does not apply to it.
 *
 * THAT `nop' SERIALIZES PENDING WRITES ON THE 68060 IS ASSUMED, NOT MEASURED --
 * owed a read of the MC68060UM before the store-buffer rung, on the discipline
 * that made the port lane settle PCR bit 1 from the manual before any PCR code.
 */
static void
zz9000eth_sync()
{
    if (zz9000eth_cache == 0)
	return;
    ZZ9000ETH_NOP();
    zz9000eth_sync_n++;
}
#else	/* !ZZ9000ETH_KVMAP: the stock 68030 kernel */
#define zz9000eth_ci_check(b)	0
#define zz9000eth_push(va, len)	((void)0)
#define zz9000eth_inval(va, len)	((void)0)
#define zz9000eth_sync()	((void)0)
#endif	/* ZZ9000ETH_KVMAP */

/*
 * Is hi a MAC_HI word (MAC bytes 0 and 1) the firmware can return?  A station
 * address is unicast (byte 0, bit 0 clear) and not 00:00, and a Zorro bus
 * that nothing drives reads 0xFFFF.  The address itself is a firmware setting
 * (68:82:F2:00:01:00 unless someone wrote another), so no prefix is checked.
 * AutoConfig already vouches for the board; this only asks whether its
 * firmware's ethernet answers.
 */
static int
zz9000eth_machi_ok(hi)
unsigned long hi;
{
    return (hi & 0x0100) == 0 && hi != 0 && hi != 0xFFFF;
}

/* ----------------------------------------------------------------------------
 * Discovery + mapping.  Fills board->zz9000eth_info {board_base, product,
 * regs, frame, txwin, paddress}.  0 on success; ENXIO when no ZZ9000 is
 * present (silently) or its ethernet does not answer; ENOMEM on a mapping
 * failure.
 *
 * AutoConfig only: MNT product 3 (Zorro II) first, as the graphics driver
 * finds the board, then product 4 (Zorro III).  Nothing is read before
 * autocon() has named a board.
 *
 * MAPPING (z3660eth's, see its z3660eth_map() comment for the argument).  A
 * window that lies wholly below VSECT1 (0x40000000) is reached through the
 * identity map of section 0 -- a Zorro II ZZ9000 always (0x200000..0x9FFFFF):
 * no mapping call, nothing taken from sptmap.  A Zorro III board above it is
 * mapped: noncacheable through dev_kvmap on the 040/060 line
 * (ZZ9000ETH_KVMAP), through sptalloc() on the stock 030 line, where nothing
 * else exists and the data cache is off.  Only the register page and
 * 0x2000..0x9FFF are mapped, never the framebuffer.
 * ------------------------------------------------------------------------- */
static int
zz9000eth_map(board_index)
int board_index;
{
    zz9000eth_board_t *board = &zz9000eth_board[board_index];
    zz9000eth_info_t *info = &board->zz9000eth_info;
    long base, size;
    unsigned long hi, mid, lo, bp;
    int e;

    if (info->regs)
	return 0;
#ifdef ZZ9000ETH_KVMAP
    /* arm the line ops for the loader's CPU class unless set by hand
     * (the direct-map arm needs them on real silicon) */
    if (zz9000eth_cache == 0 && (zz9000eth_cputype == 40 || zz9000eth_cputype == 60))
	zz9000eth_cache = zz9000eth_cputype;
#endif
    if (info->map_verdict) {		/* probed before and failed */
	if (!zz9000eth_reprobe)
	    return info->map_verdict;
	zz9000eth_reprobe = 0;
	info->map_verdict = 0;
    }

    if (autocon(ZZ9000ETH_PROD_Z2, board_index, &base, &size))
	info->product = 3;
    else if (autocon(ZZ9000ETH_PROD_Z3, board_index, &base, &size))
	info->product = 4;
    else
	return zz9000eth_absent(board_index, ENXIO, "no board", ZZ_ABS_QUIET);

    info->board_base = base;
    bp = (unsigned long)base;
    zz9000eth_board_phys = bp;

    /*
     * Whole window inside the identity-mapped section 0?  Then the physical
     * address is the kernel VA.  The subtraction cannot wrap: bp < VSECT1.
     */
    if (bp < (unsigned long)VSECT1 &&
	((unsigned long)VSECT1 - bp) >= (unsigned long)ZZ_WINDOW_TOP) {
	zz9000eth_direct_map = 1;
	info->spt_mapped = 0;
	info->regs  = (volatile unsigned char *)phystokv((paddr_t)base);
	info->frame = (volatile unsigned char *)
			phystokv((paddr_t)base + ZZ_FRAME_OFF);
    } else {
	zz9000eth_direct_map = 0;
#ifdef ZZ9000ETH_KVMAP
	info->spt_mapped = ZZ_KVMAPPED;	/* from here every failure must unmap */
	info->regs = (volatile unsigned char *)dev_kvmap(bp,
			(unsigned long)ZZ_REGS_BYTES, ZZ_CM_NCS, 0);
	info->frame = (volatile unsigned char *)dev_kvmap(bp + ZZ_FRAME_OFF,
			(unsigned long)ZZ_FRAME_BYTES, ZZ_CM_NC, 0);
#else
	info->spt_mapped = 1;		/* from here every failure must unmap */
	info->regs = (volatile unsigned char *)sptalloc(ZZ_REGS_PAGES, PG_V,
			phystopfn((paddr_t)base), 0);
	info->frame = (volatile unsigned char *)sptalloc(ZZ_FRAME_PAGES, PG_V,
			phystopfn((paddr_t)base + ZZ_FRAME_OFF), 0);
#endif
    }
    if (info->regs == 0 || info->frame == 0) {
	/*
	 * Mapping failure (sptmap exhausted), or a base of 0.  Free whichever
	 * half DID map.  Not cached: VA pressure is transient, so the next open
	 * tries again -- which is safe now that nothing is kept on failure.
	 */
	zz9000eth_unmap(info);
	zz9000eth_map_fails++;
	if (!info->nomem_said) {
	    info->nomem_said = 1;
	    cmn_err(CE_NOTE,
		"zz9000eth: ZZ9000 Zorro %s ethernet not mapped (no kernel VA); "
		ZZ9000ETH_RETRY, info->product == 3 ? "II" : "III");
	}
	return ENOMEM;
    }
    info->txwin = info->frame + ZZ_TXWIN_OFF;

    /*
     * F3-M0: assert the free ride BEFORE the first register read, so a stale
     * cache line cannot be mistaken for a firmware answer.  A no-op with
     * zz9000eth_cache == 0 (every 030 image).
     */
    if ((e = zz9000eth_ci_check(board_index)) != 0)
	return zz9000eth_absent(board_index, e, "cache-inhibit check refused",
			       ZZ_ABS_SAY);

    /* does the firmware's ethernet answer?  (a ZZ9000 is there: AutoConfig) */
    hi = ZZ_RD16(info->regs, ZZ_ETH_MAC_HI);
    mid = ZZ_RD16(info->regs, ZZ_ETH_MAC_HI2);
    lo = ZZ_RD16(info->regs, ZZ_ETH_MAC_LO);
    zz9000eth_machi = hi;
    zz9000eth_maclo = (mid << 16) | lo;
    if (!zz9000eth_machi_ok(hi))
	return zz9000eth_absent(board_index, ENXIO, "MAC reads",
			       ZZ_ABS_SAY | ZZ_ABS_MAC);

    info->paddress[0] = (unsigned char)(hi >> 8);
    info->paddress[1] = (unsigned char)hi;
    info->paddress[2] = (unsigned char)(mid >> 8);
    info->paddress[3] = (unsigned char)mid;
    info->paddress[4] = (unsigned char)(lo >> 8);
    info->paddress[5] = (unsigned char)lo;

    zz9000eth_present = 1;
    return 0;
}

/*
 * Undo zz9000eth_map()'s mapping.  THE RULE: map, probe, free on failure.  The
 * sptalloc arm draws from sptmap, a fixed kernel-VA resource map that nothing
 * else returns pages to; before 2026-09-27 every failed probe kept 1 + ZZ_FRAME_PAGES
 * pages (33 at NBPP 4096, 65 at 2048); on the emulator S99zen's 16 boot-time
 * opens left 18 free sptmap pages (549 without S99zen), so raw I/O and big kmem
 * pools slept forever on Km_BigWanted.
 * flag 0 = unmap only: these are board pages, never page[] frames to release.
 * The direct-map arm is an identity VA with nothing to give back.  close() never
 * unmaps (a mapped board stays mapped for the kernel's life, and map() returns
 * early on it), so this is the only teardown path.
 */
static void
zz9000eth_unmap(info)
zz9000eth_info_t *info;
{
#ifdef ZZ9000ETH_KVMAP
    if (info->spt_mapped == ZZ_KVMAPPED) {
	if (info->regs)
	    dev_kvunmap((caddr_t)info->regs, (unsigned long)ZZ_REGS_BYTES);
	if (info->frame)
	    dev_kvunmap((caddr_t)info->frame, (unsigned long)ZZ_FRAME_BYTES);
	info->spt_mapped = 0;
    }
#endif
    if (info->spt_mapped) {
	if (info->regs) {
	    sptfree((caddr_t)info->regs, ZZ_REGS_PAGES, 0);
	    zz9000eth_spt_freed += ZZ_REGS_PAGES;
	}
	if (info->frame) {
	    sptfree((caddr_t)info->frame, ZZ_FRAME_PAGES, 0);
	    zz9000eth_spt_freed += ZZ_FRAME_PAGES;
	}
	info->spt_mapped = 0;
    }
    info->regs = 0;
    info->frame = 0;
    info->txwin = 0;
}

/*
 * A failure verdict: unmap, cache errno e for this board so later opens return
 * it without mapping or probing, and say so on the console ONCE (per verdict).
 * how: ZZ_ABS_SAY for a board that is there (AutoConfig lists it), named by
 * its Zorro type; ZZ_ABS_QUIET for a machine without a ZZ9000, which is
 * normal and says nothing (a line with -DZZ9000ETH_DEBUG); ZZ_ABS_MAC appends
 * the MAC register words the probe read.
 */
static int
zz9000eth_absent(board_index, e, why, how)
int board_index, e;
char *why;
int how;
{
    zz9000eth_info_t *info = &zz9000eth_board[board_index].zz9000eth_info;

    zz9000eth_unmap(info);
    zz9000eth_present = 0;
    zz9000eth_map_fails++;
    info->map_verdict = e;
#ifndef ZZ9000ETH_DEBUG
    if (!(how & ZZ_ABS_SAY))
	return e;
#endif
    if (!(how & ZZ_ABS_SAY))
	cmn_err(CE_NOTE, "zz9000eth: no ZZ9000 (%s); " ZZ9000ETH_FAILS " %d",
	    why, e);
    else if (how & ZZ_ABS_MAC)
	cmn_err(CE_NOTE,
	    "zz9000eth: ZZ9000 Zorro %s: ethernet does not answer (%s 0x%x 0x%x); "
	    ZZ9000ETH_FAILS " %d", info->product == 3 ? "II" : "III",
	    why, zz9000eth_machi, zz9000eth_maclo, e);
    else
	cmn_err(CE_NOTE, "zz9000eth: ZZ9000 Zorro %s: %s; " ZZ9000ETH_FAILS " %d",
	    info->product == 3 ? "II" : "III", why, e);
    return e;
}

void
zz9000eth_autoconfig()
{
    if (zz9000eth_autoconfigured)
	return;
    zz9000eth_autoconfigured = 1;
    zz9000eth_number_of_boards = 0;

    /* a failure prints its own single line from zz9000eth_map() */
    if (zz9000eth_map(0) == 0)
	zz9000eth_number_of_boards = 1;
}

/*
 * Bring a board up: ensure mapped, init the per-stream array, enable firmware
 * eth interrupt delivery, and start the RX poll callout.  Returns 0, or the
 * errno zz9000eth_map() gave (ENXIO absent -- cached; ENOMEM no kernel VA).
 */
static int
zz9000eth_initialize(board_index)
int board_index;
{
    zz9000eth_board_t *board = &zz9000eth_board[board_index];
    zz9000eth_info_t *info = &board->zz9000eth_info;
    zz9000eth_t *zp;
    int n, e;

    if ((e = zz9000eth_map(board_index)) != 0)
	return e;

    for (n = 0, zp = board->zz9000eth; n < ZZ9000ETH_MAXDEV; ++n, ++zp) {
	zp->q = (queue_t *)NULL;
	zp->sap = 0;
	zp->state = DL_UNBOUND;
    }

    info->rx_held = 0;
    info->tx_busy = 0;

    bzero((caddr_t)&board->zz9000eth_status, sizeof(board->zz9000eth_status));
    board->zz9000eth_status.board_state = ZZ9000ETH_BOARD_RUNNING;

    /*
     * Do NOT enable the firmware's ethernet INT6 (write 0, not 1): AMIX has no
     * level-6 handler for it, p6int's CIA-B handler never acknowledges it, and
     * the level-6 interrupt would storm (z3660eth's lesson on real hardware,
     * 2026-06-21, from the same firmware logic: main.c raises it while the
     * backlog is not empty and the enable bit is set).  The backlog fills
     * regardless of the bit, so 0 keeps the datapath and clears a pending
     * one.  The graphics driver never writes this register.
     */
    ZZ9000ETH_CONFIG(info->regs, ZZ_CONFIG_DISABLE);

    /*
     * Where the ring stands.  The firmware's serials run on from whatever came
     * before (AmigaOS, an earlier AMIX boot), and the window may hold a frame
     * nobody consumed: take its serial as the last one seen and consume it, so
     * the drain starts in step -- one stale frame dropped, none delivered, and
     * no slot left unconsumed for ever (with the older-serial rule in
     * zz9000eth_drain, a stale slot that is never strobed would hold the
     * backlog until it overflows).
     */
    zz9000eth_inval((unsigned long)info->frame, (unsigned long)ZZ_RX_FRAME_PAD);
    info->last_serial = (info->frame[2] << 8) | info->frame[3];
    ZZ9000ETH_RX_STROBE(info->regs);
    zz9000eth_sync();

    if (!info->poll_running) {
	info->poll_running = 1;
	info->poll_id = timeout(zz9000eth_poll, (caddr_t)(long)board_index,
				ZZ9000ETH_POLL_TICKS);
    }
    return 0;
}

/* ----------------------------------------------------------------------------
 * STREAMS open/close -- clone/dev_t machinery copied from hydra/aen (the double
 * open under /dev/ip and /dev/arp depends on the makedevice() minor rewrite).
 * ------------------------------------------------------------------------- */
static int
zz9000ethopen(q, devp, flag, sflag, credp)
register queue_t *q;
dev_t *devp;
int flag, sflag;
cred_t *credp;
{
    register zz9000eth_t *zp;
    register int minor_device, board_index, sap_index;
    zz9000eth_board_t *board;
    int s, e;

    if (sflag == MODOPEN || sflag == CLONEOPEN)
	return EINVAL;

    zz9000eth_autoconfig();

    minor_device = getminor(*devp);
    board_index = minor_device & 0x0F;
    sap_index = (minor_device & 0xF0) >> 4;

    if (board_index >= ZZ9000ETH_MAXBOARDS)
	return ENODEV;
    board = &zz9000eth_board[board_index];

    if (board->zz9000eth_status.board_state != ZZ9000ETH_BOARD_RUNNING) {
	/* no ZZ9000 (an emulator, another card): ENXIO, cached, silent */
	if ((e = zz9000eth_initialize(board_index)) != 0)
	    return e;
    }
    if (board->zz9000eth_info.regs == 0)
	return ENXIO;

    if (sap_index == 0) {
	for (sap_index = 0, zp = board->zz9000eth;
	     sap_index < ZZ9000ETH_MAXDEV;
	     sap_index++, zp++)
	    if (!zp->q)
		break;
    } else
	--sap_index;

    if (sap_index >= ZZ9000ETH_MAXDEV)
	return ENOSPC;

    *devp = makedevice(getmajor(*devp), (((sap_index + 1) << 4) | board_index));

    zp = &board->zz9000eth[sap_index];

    /*
     * M2: publish the slot atomically w.r.t. the poll callout, whose RX demux
     * (toss_packet_up_stream) routes on zp->q, flags and sap.  close() clears
     * only zp->q, so without the mask a tick between `zp->q = q' and the field
     * resets would deliver frames up the new stream under the PREVIOUS
     * occupant's sap / RAW flag.  zp->q is still stored last.
     */
    s = splzz9000eth();
    if (zp->q) {
	splx(s);
	return EBUSY;
    }
    q->q_ptr = (char *)zp;
    WR(q)->q_ptr = (char *)zp;
    zp->board_index = board_index;
    zp->flags = 0;
    zp->state = DL_UNATTACHED;
    zp->sap = 0;
    zp->q = q;
    splx(s);

    if (!board->ifstats.ifs_name) {
	board->ifstats.ifs_name = "zz";
	board->ifstats.ifs_mtu = ZZ9000ETH_MTU;
	board->ifstats.ifs_next = ifstats;
	ifstats = &board->ifstats;
    }
    board->ifstats.ifs_unit = board_index;
    board->ifstats.ifs_active = 1;
    board->if_flags = IFF_BROADCAST | IFF_NOTRAILERS | IFF_RUNNING;

    return 0;
}

static int
zz9000ethclose(q)
register queue_t *q;
{
    register zz9000eth_t *zp = (zz9000eth_t *)q->q_ptr;
    register zz9000eth_board_t *board = &zz9000eth_board[zp->board_index];
    zz9000eth_info_t *info = &board->zz9000eth_info;
    int dev;

    zp->q = 0;
    OTHERQ(q)->q_ptr = NULL;
    q->q_ptr = NULL;

    for (dev = 0, zp = board->zz9000eth; dev < ZZ9000ETH_MAXDEV; ++dev, ++zp)
	if (zp->q)
	    break;

    if (dev >= ZZ9000ETH_MAXDEV) {		/* last stream closed */
	if (info->poll_running) {
	    info->poll_running = 0;
	    untimeout(info->poll_id);
	}
	if (info->regs)
	    ZZ9000ETH_CONFIG(info->regs, ZZ_CONFIG_DISABLE);
	board->zz9000eth_status.board_state = ZZ9000ETH_BOARD_RESET;
	board->ifstats.ifs_active = 0;
    }
    return 0;
}

static int
zz9000ethwput(q, mp)
register queue_t *q;
register mblk_t *mp;
{
    switch (mp->b_datap->db_type) {
    case M_FLUSH:
	if (*mp->b_rptr & FLUSHW) {
	    flushq(q, FLUSHALL);
	    *mp->b_rptr &= ~FLUSHW;
	}
	if (*mp->b_rptr & FLUSHR)
	    qreply(q, mp);
	else
	    freemsg(mp);
	break;

    case M_PROTO:
    case M_PCPROTO:
	/*
	 * NOT masked as a whole (the hydra idiom was): proto reaches xmit, and
	 * a mask here would hold IPL 4 across the ~1 ms TX trigger.  The few
	 * sections that race the poll callout mask themselves (z3660net NOTES.md).
	 */
	(void)zz9000ethproto(q, mp);
	break;

    case M_IOCTL:
    case M_IOCDATA:
	zz9000ethioctl(q, mp);
	break;

    default:
	freemsg(mp);
	break;
    }
    return 0;
}

static int
zz9000ethwsrv(q)
queue_t *q;
{
    mblk_t *mp;

    /* drain anything deferred by xmit; no TX-completion IRQ exists */
    while ((mp = getq(q)) != NULL) {
	if (!zz9000ethxmit(q, mp)) {
	    putbq(q, mp);
	    break;
	}
    }
    return 0;
}

/*
 * Transmit one DL_UNITDATA_REQ.  Returns 1 if the mblk was consumed (sent or
 * dropped), 0 if it must be re-queued (txwin busy with another transmit).  The
 * single shared txwin is serialised by info->tx_busy WITHOUT holding raised spl
 * across the ~1ms synchronous trigger (per z3660net ETHERNET-SCOPING.md Q2/Q7).  Only the
 * claim (test-and-set) is masked; the release is one aligned store of a volatile
 * int and needs no mask.  See z3660net NOTES.md "Interrupt masking", pair M1.
 */
static int
zz9000ethxmit(q, mp)
queue_t *q;
mblk_t *mp;
{
    register zz9000eth_t *zp = (zz9000eth_t *)q->q_ptr;
    zz9000eth_board_t *board = &zz9000eth_board[zp->board_index];
    zz9000eth_info_t *info = &board->zz9000eth_info;
    dl_unitdata_req_t *dp = (dl_unitdata_req_t *)mp->b_rptr;
    static unsigned char tmp[ETH_MAXPACKET + 2];
    unsigned char *ap;
    mblk_t *m;
    int s, n, pktsize;
    unsigned long rc;

    s = splzz9000eth();			/* M1: atomic claim of txwin + tmp[] */
    if (info->tx_busy || info->regs == 0) {
	splx(s);
	return 0;			/* defer; caller re-queues */
    }
    info->tx_busy = 1;
    splx(s);

    pktsize = ETH_HEADER_SIZE;
    ap = mp->b_rptr + dp->dl_dest_addr_offset;
    bcopy((caddr_t)ap, (caddr_t)&tmp[0], ETH_ADDRESS_SIZE);
    bcopy((caddr_t)info->paddress, (caddr_t)&tmp[6], ETH_ADDRESS_SIZE);
    bcopy((caddr_t)&zp->sap, (caddr_t)&tmp[12], ETH_TYPE_SIZE);  /* big-endian, no swap */
    board->zz9000eth_status.last_tx_sap = zp->sap;

    n = ETH_MAXDATA;
    for (m = mp->b_cont; m && n > 0; m = m->b_cont) {
	int copy = min(blklen(m), n);		/* n>0 here, so copy is never negative */
	if (copy <= 0)
	    continue;				/* skip empty mblk; never overrun tmp[] */
	bcopy((caddr_t)m->b_rptr, (caddr_t)&tmp[pktsize], copy);
	pktsize += copy;
	n -= copy;
    }
    freemsg(mp);

    if (pktsize < ETH_MINPACKET) {
	bzero((caddr_t)&tmp[pktsize], ETH_MINPACKET - pktsize);
	pktsize = ETH_MINPACKET;
    }

    bcopy((caddr_t)tmp, (caddr_t)info->txwin, pktsize);
    /*
     * Push the frame out of the data cache before the trigger, so the ARM reads
     * current bytes.  tmp[] itself is NOT maintained: kernel BSS with exactly one
     * alias, written and read by the CPU only, so it is self-coherent under any
     * cache mode -- maintaining it would only drag its BSS neighbours into a
     * rounded range.  (z3660net docs/BLIZZARD-F3.md 5.1)
     */
    zz9000eth_push((unsigned long)info->txwin, (unsigned long)pktsize);
    zz9000eth_sync();

    /*
     * trigger: synchronous, up to ~1 ms (ethernet_send_frame waits for the
     * GEM) -- no spl section around it.  The result reads back as result << 8
     * (main.c REG_ZZ_ETH_TX read); the barrier keeps the result load behind
     * the trigger store.
     */
    ZZ_WR16(info->regs, ZZ_ETH_TX, pktsize);
    zz9000eth_sync();
    rc = (ZZ_RD16(info->regs, ZZ_ETH_TX) >> 8) & 0xFF;

    info->tx_busy = 0;			/* release: volatile store, after the rc load */

    if (rc == 0) {
	board->zz9000eth_status.packets_sent++;
	board->ifstats.ifs_opackets++;
	if (zp->sap == ETHERTYPE_ARP) board->zz9000eth_status.tx_arp++;
	if (zp->sap == ETHERTYPE_IP)  board->zz9000eth_status.tx_ip++;
    } else if (rc == 1) {
	board->zz9000eth_status.tx_nolink++;	/* no carrier yet; frame dropped */
    } else {
	board->zz9000eth_status.tx_errors++;
	board->ifstats.ifs_oerrors++;
    }
    return 1;
}

/* ----------------------------------------------------------------------------
 * RX backlog drain.  Reads the slot in the RX window every iteration (the
 * firmware moves the window to the next slot on the ZZ_ETH_RX strobe), stops
 * at a serial that is not ahead of the last one consumed, strobes to free each
 * slot.  "Ahead", not "different": once the driver has caught up the firmware
 * resets its backlog and the window shows slot 0 again, which still holds the
 * FIRST frame of the round just consumed -- an older serial (the Z3660
 * firmware rewrites that serial at catch-up; the ZZ9000's does not).  Callable from the poll callout, the read service procedure
 * (masked to the callout's IPL 4 there) OR a future ISR.
 *
 * BACKPRESSURE.  A frame is never freed because an upstream queue is full.  The
 * verdict (zz9000eth_rx_want) comes from the header alone, BEFORE the slot is
 * released: if a stream that wants the frame cannot take it now, the drain stops
 * and leaves the frame, and everything behind it, in the firmware backlog.  The
 * failed canput() marks that upstream queue QWANTW; when it drains, STREAMS
 * back-enables our read queue and zz9000ethrsrv resumes here, usually within the
 * same tick, and otherwise the next poll tick does.  Freeing the frame instead
 * (the old shape) lost every frame past the upstream queue's high-water mark in
 * one drain: on metal 4 in 5 datagrams at 1600/s and the TCP push's retransmits.
 *
 * The hold is bounded (zz9000eth.h, ZZ9000ETH_RX_HOLD_TICKS): past it the
 * frame goes to every stream that can take it and is dropped for the rest
 * (couldnt_put), so a stuck stream cannot wedge the interface; meanwhile the
 * firmware tail-drops what a full backlog (32) cannot take.
 * ------------------------------------------------------------------------- */

/*
 * Which streams want this frame -- the same demux as toss_packet_up_stream --
 * and which of them can take it now.  Bit j stands for stream j.  canput() on a
 * full queue also sets QWANTW there, which is what back-enables zz9000ethrsrv.
 * Reads only the header (`packet' holds ZZ9000ETH_RX_PEEK bytes at this point).
 */
static void
zz9000eth_rx_want(board_index, packet, wantp, readyp)
int board_index;
unsigned char *packet;
int *wantp, *readyp;
{
    zz9000eth_board_t *board = &zz9000eth_board[board_index];
    zz9000eth_t *zp = board->zz9000eth;
    ether_header_t *eh = (ether_header_t *)packet;
    int j, want = 0, ready = 0;

    for (j = 0; j < ZZ9000ETH_MAXDEV; j++, zp++) {
	if (!zp->q)
	    continue;
	if (!(zp->flags & ZZ9000ETH_RAW_DATA)) {
	    if (zp->sap != eh->ether_type)
		continue;
	    if (bcmp((char *)eh->ether_dhost,
		     (char *)board->zz9000eth_info.paddress, ETH_ADDRESS_SIZE) &&
		bcmp((char *)eh->ether_dhost,
		     (char *)zz9000eth_bcast, ETH_ADDRESS_SIZE))
		continue;
	}
	want |= 1 << j;
	if (canput(zp->q->q_next))
	    ready |= 1 << j;
    }
    *wantp = want;
    *readyp = ready;
}



static void
zz9000eth_drain(board_index)
int board_index;
{
    zz9000eth_board_t *board = &zz9000eth_board[board_index];
    zz9000eth_info_t *info = &board->zz9000eth_info;
    static unsigned char rxbuff[MAX_BUFFER_LENGTH];
    volatile unsigned char *slot = info->frame;	/* the firmware's current slot */
    unsigned short serial, ahead;
    int len, loop = 0, want, ready;

    if (info->regs == 0)
	return;
    board->zz9000eth_status.drain_calls++;

    while (++loop <= 256) {
	/*
	 * Invalidate the header line before reading it, every iteration: the
	 * firmware publishes the header after the payload (ethernet.c: memcpy,
	 * then len at +0..1 and the serial at +2..3), and after a strobe the
	 * same window shows another slot -- so the header may never be read
	 * from a line that survived a strobe.  The barrier after each strobe
	 * (zz9000eth_sync) keeps this read behind it.
	 */
	zz9000eth_inval((unsigned long)slot, (unsigned long)ZZ_RX_FRAME_PAD);
	serial = (slot[2] << 8) | slot[3];
	ahead = (unsigned short)(serial - info->last_serial);
	if (ahead == 0 || ahead >= 0x8000) {
	    if (ahead)
		zz9000eth_rx_stale++;	/* the caught-up slot 0: older */
	    info->rx_held = 0;		/* ring drained (a held frame, if any, */
	    break;			/* went with a firmware backlog reset) */
	}
	if (ahead != 1)
	    zz9000eth_rx_skipped += ahead - 1;

	len = (slot[0] << 8) | slot[1];
	board->zz9000eth_status.last_serial = serial;
	board->zz9000eth_status.last_rx_len = len;

	if (len < ETH_HEADER_SIZE || len > MAX_BUFFER_LENGTH) {
	    board->zz9000eth_status.rx_bad_size++;
	    board->zz9000eth_status.rx_errors++;
	    board->ifstats.ifs_ierrors++;
	    info->last_serial = serial;
	    info->rx_held = 0;
	    ZZ9000ETH_RX_STROBE(info->regs);
	    zz9000eth_sync();
	    continue;				/* payload never read: no invalidate */
	}

	/*
	 * Only now, AFTER the serial has certified the payload, invalidate it.
	 * The outward 16-byte rounding covers the header line (already consumed)
	 * and the slot's unused tail -- both device-written, never CPU-written,
	 * so the invalidate can discard nothing.  rxbuff[] and the mblk it feeds
	 * are NOT maintained: single alias, CPU only, never seen by the device.
	 * The header comes first, for the verdict; the rest only if a stream
	 * takes the frame (a frame nobody wants is never copied in full).
	 */
	zz9000eth_inval((unsigned long)(slot + ZZ_RX_FRAME_PAD), (unsigned long)len);
	bcopy((caddr_t)(slot + ZZ_RX_FRAME_PAD), (caddr_t)rxbuff,
	      min(len, ZZ9000ETH_RX_PEEK));
	zz9000eth_rx_want(board_index, rxbuff, &want, &ready);

	if (want & ~ready) {
	    /* a stream that wants it is full: hold, unless a guard has tripped */
	    if (!info->rx_held || info->rx_hold_serial != serial) {
		info->rx_held = 1;
		info->rx_hold_serial = serial;
		info->rx_hold_since = (long)lbolt;
	    }
	    if ((long)lbolt - info->rx_hold_since >= ZZ9000ETH_RX_HOLD_TICKS)
		zz9000eth_rx_force_ticks++;
	    else {
		zz9000eth_rx_holds++;
		break;				/* leave it in the ring */
	    }
	}

	if ((want & ready) && len > ZZ9000ETH_RX_PEEK)
	    bcopy((caddr_t)(slot + ZZ_RX_FRAME_PAD + ZZ9000ETH_RX_PEEK),
		  (caddr_t)(rxbuff + ZZ9000ETH_RX_PEEK), len - ZZ9000ETH_RX_PEEK);

	info->last_serial = serial;
	info->rx_held = 0;
	ZZ9000ETH_RX_STROBE(info->regs);	/* advance/free the slot (ZS-05: armed) */
	zz9000eth_sync();		/* next cursor read must not pass the strobe */

	board->zz9000eth_status.packets_received++;
	board->ifstats.ifs_ipackets++;

	/*
	 * NOTE (Q4, z3660net ETHERNET-SCOPING.md): `len` is the firmware-reported frame
	 * length.  Whether it includes the 4-byte FCS is real-HW-confirmable; if
	 * a one-frame probe shows trailing FCS, subtract ETH_CRC_LEN here.  The
	 * scoping verdict is "do NOT subtract by default" -- delivered as-is.
	 */
	toss_packet_up_stream(rxbuff, board_index, len, ready);
    }
}

/* The clock-level poll callout: drain RX, then any deferred TX, then re-arm. */
static void
zz9000eth_poll(arg)
caddr_t arg;
{
    int board_index = (int)(long)arg;
    zz9000eth_board_t *board = &zz9000eth_board[board_index];
    zz9000eth_info_t *info = &board->zz9000eth_info;

    if (panicstr)
	return;

    if (board->zz9000eth_status.board_state == ZZ9000ETH_BOARD_RUNNING) {
	zz9000eth_drain(board_index);
	zz9000eth_tx_drain(board_index);
    }
    if (info->poll_running)
	info->poll_id = timeout(zz9000eth_poll, arg, ZZ9000ETH_POLL_TICKS);
}

/*
 * Read service procedure.  Nothing is ever queued on the read side: STREAMS
 * back-enables this queue when an upstream queue that refused a frame (the
 * canput in zz9000eth_rx_want) has drained below its low-water mark, and this
 * resumes the drain then, instead of a tick later.  It runs from queuerun,
 * outside the poll callout, so the drain is masked here to the callout's own
 * IPL 4: the two drainers never interleave on the ring, rxbuff or last_serial.
 * Holding IPL 4 across the drain is what the callout does already.
 */
static int
zz9000ethrsrv(q)
queue_t *q;
{
    zz9000eth_t *zp = (zz9000eth_t *)q->q_ptr;
    zz9000eth_board_t *board;
    int s;

    if (zp == NULL || panicstr)
	return 0;
    board = &zz9000eth_board[zp->board_index];
    s = splzz9000eth();
    if (board->zz9000eth_status.board_state == ZZ9000ETH_BOARD_RUNNING &&
	board->zz9000eth_info.rx_held) {
	zz9000eth_rx_resumed++;
	zz9000eth_drain(zp->board_index);
    }
    splx(s);
    return 0;
}

/*
 * Drain frames deferred by xmit (txwin-busy).  Mirrors hydra transmit_interrupt.
 *
 * A stream that cannot transmit right now ends ITS drain (break), not the whole
 * sweep: `return` here -- the inherited hydra shape -- starved every stream after
 * the first blocked one for a full poll tick, even though the blocking condition
 * (info->tx_busy, or regs == 0) is per-board and the next stream would simply
 * defer too.  The cost of continuing is one getq/putbq pair per remaining stream
 * (ZZ9000ETH_MAXDEV is 5); the cost of returning was a whole tick of head-of-line
 * blocking for every other bound SAP.
 */
static void
zz9000eth_tx_drain(board_index)
int board_index;
{
    zz9000eth_t *zp = zz9000eth_board[board_index].zz9000eth;
    mblk_t *mp;
    int n;

    for (n = 0; n < ZZ9000ETH_MAXDEV; n++, zp++) {
	if (zp->q) {
	    queue_t *wrq = WR(zp->q);
	    while ((mp = getq(wrq)) != NULL) {
		if (!zz9000ethxmit(wrq, mp)) {
		    putbq(wrq, mp);
		    break;		/* this stream only -- try the next */
		}
	    }
	}
    }
}

/*
 * Interrupt service for a future interrupt-driven RX path (not wired into
 * any kernel -- the ZZ9000 interrupts on level 6, and AMIX has no level-6
 * driver chain yet).  Reads the pending bits (ZZ_CONFIG), acks ETH, and
 * drains.  The firmware raises the interrupt only while CONFIG's enable bit is
 * set, which this driver never sets today.
 *
 * WARNING: shares the static rxbuff in zz9000eth_drain with the poll callout and
 * zz9000ethrsrv.  If this ISR is ever registered (int2_tbl via board-mod, or an
 * int6_tbl change), DISABLE the poll first and check that zz9000ethrsrv's mask
 * still covers its level -- do not run two drainers concurrently.
 */
void
zz9000ethintr()
{
    int i;

    if (panicstr)
	return;

    for (i = 0; i < ZZ9000ETH_MAXBOARDS; i++) {
	zz9000eth_board_t *board = &zz9000eth_board[i];
	zz9000eth_info_t *info = &board->zz9000eth_info;

	if (board->zz9000eth_status.board_state == ZZ9000ETH_BOARD_RUNNING &&
	    info->regs != 0) {
	    unsigned long st;

	    st = ZZ_RD16(info->regs, ZZ_CONFIG);	/* pending bits */
	    if (st & ZZ_INT_ETH) {
		ZZ9000ETH_CONFIG(info->regs, ZZ_CONFIG_ACK_ETH);
		zz9000eth_drain(i);
	    }
	}
    }
}

/* ----------------------------------------------------------------------------
 * DLPI protocol engine -- the seven primitives, copied from hydra (Style-1
 * connectionless ethernet).  DL_UNITDATA_REQ feeds zz9000ethxmit.
 * ------------------------------------------------------------------------- */
static int
zz9000ethproto(q, mp)
queue_t *q;
mblk_t *mp;
{
    register union DL_primitives *p = (union DL_primitives *)mp->b_rptr;
    register zz9000eth_t *zp = (zz9000eth_t *)q->q_ptr;
    zz9000eth_board_t *board = &zz9000eth_board[zp->board_index];

    switch (p->dl_primitive) {
    case DL_UNITDATA_REQ:
	if (q->q_first || !zz9000ethxmit(q, mp))
	    putq(q, mp);
	break;

    case DL_ATTACH_REQ:
    {
	dl_ok_ack_t *okp;
	freemsg(mp);
	if (!(mp = allocb(sizeof(dl_ok_ack_t), BPRI_MED)))
	    return 1;
	okp = (dl_ok_ack_t *)mp->b_wptr;
	mp->b_datap->db_type = M_PCPROTO;
	okp->dl_primitive = DL_OK_ACK;
	okp->dl_correct_primitive = DL_ATTACH_REQ;
	mp->b_wptr += sizeof(dl_ok_ack_t);
	zp->state = DL_UNBOUND;
	qreply(q, mp);
	return 1;
    }

    case DL_DETACH_REQ:
    {
	dl_ok_ack_t *okp;
	freemsg(mp);
	if (!(mp = allocb(sizeof(dl_ok_ack_t), BPRI_MED)))
	    return 1;
	okp = (dl_ok_ack_t *)mp->b_wptr;
	mp->b_datap->db_type = M_PCPROTO;
	okp->dl_primitive = DL_OK_ACK;
	okp->dl_correct_primitive = DL_DETACH_REQ;
	mp->b_wptr += sizeof(dl_ok_ack_t);
	zp->state = DL_UNATTACHED;
	qreply(q, mp);
	return 1;
    }

    case DL_BIND_REQ:
    {
	dl_bind_req_t *reqp = (dl_bind_req_t *)mp->b_rptr;
	dl_bind_ack_t *ackp;

	zp->sap = reqp->dl_sap;
	board->zz9000eth_status.last_bound_sap = zp->sap;
	zp->state = DL_IDLE;

	freemsg(mp);
	if (!(mp = allocb(sizeof(*ackp) + ETH_ADDRESS_SIZE, BPRI_MED)))
	    return 1;
	mp->b_datap->db_type = M_PCPROTO;
	ackp = (dl_bind_ack_t *)mp->b_wptr;
	ackp->dl_primitive = DL_BIND_ACK;
	ackp->dl_sap = zp->sap;
	ackp->dl_addr_length = ETH_ADDRESS_SIZE;
	ackp->dl_addr_offset = sizeof(*ackp);
	ackp->dl_max_conind = 0;
	ackp->dl_growth = 0;
	mp->b_wptr += sizeof(*ackp);
	bcopy((caddr_t)board->zz9000eth_info.paddress, (caddr_t)mp->b_wptr,
	      ETH_ADDRESS_SIZE);
	mp->b_wptr += ETH_ADDRESS_SIZE;
	qreply(q, mp);
	return 1;
    }

    case DL_UNBIND_REQ:
    {
	dl_ok_ack_t *okp;
	freemsg(mp);
	if (!(mp = allocb(sizeof(dl_ok_ack_t), BPRI_MED)))
	    return 1;
	okp = (dl_ok_ack_t *)mp->b_wptr;
	mp->b_datap->db_type = M_PCPROTO;
	okp->dl_primitive = DL_OK_ACK;
	okp->dl_correct_primitive = DL_UNBIND_REQ;
	mp->b_wptr += sizeof(dl_ok_ack_t);
	zp->state = DL_UNBOUND;
	zp->sap = 0;
	qreply(q, mp);
	return 1;
    }

    case DL_INFO_REQ:
    {
	dl_info_ack_t *ackp;
	freemsg(mp);
	if (!(mp = allocb(sizeof(dl_info_ack_t) + ETH_ADDRESS_SIZE, BPRI_MED)))
	    return 1;
	mp->b_datap->db_type = M_PCPROTO;
	ackp = (dl_info_ack_t *)mp->b_wptr;
	ackp->dl_primitive = DL_INFO_ACK;
	ackp->dl_max_sdu = ETH_MAXDATA;
	ackp->dl_min_sdu = 1;
	ackp->dl_addr_length = ETH_ADDRESS_SIZE;
	ackp->dl_mac_type = DL_ETHER;
	ackp->dl_reserved = 0;
	ackp->dl_current_state = zp->state;
	ackp->dl_max_idu = ETH_MAXDATA;
	ackp->dl_service_mode = DL_CLDLS;
	ackp->dl_qos_length = 0;
	ackp->dl_qos_offset = 0;
	ackp->dl_qos_range_length = 0;
	ackp->dl_qos_range_offset = 0;
	ackp->dl_provider_style = DL_STYLE1;
	ackp->dl_growth = 0;
	if (zp->state == DL_IDLE) {
	    ackp->dl_addr_offset = sizeof(dl_info_ack_t);
	    mp->b_wptr += sizeof(dl_info_ack_t);
	    bcopy((caddr_t)board->zz9000eth_info.paddress, (caddr_t)mp->b_wptr,
		  ETH_ADDRESS_SIZE);
	    mp->b_wptr += ETH_ADDRESS_SIZE;
	} else {
	    ackp->dl_addr_offset = 0;
	    mp->b_wptr += sizeof(dl_info_ack_t);
	}
	qreply(q, mp);
	return 1;
    }

    /*
     * Anything else: DLPI requires an explicit DL_ERROR_ACK naming the offending
     * primitive (dl_errno = DL_BADPRIM, "primitive not known by the provider").
     * Silently freeing the message -- the inherited behaviour -- left the DLS
     * user waiting for an ack that would never arrive.  Built the same way as the
     * other acks above: the primitive is saved first, because `p` points into the
     * message we are about to free.
     */
    default:
    {
	dl_error_ack_t *ep;
	unsigned long bad_primitive = p->dl_primitive;

	freemsg(mp);
	if (!(mp = allocb(sizeof(dl_error_ack_t), BPRI_MED)))
	    return 1;
	ep = (dl_error_ack_t *)mp->b_wptr;
	mp->b_datap->db_type = M_PCPROTO;
	ep->dl_primitive = DL_ERROR_ACK;
	ep->dl_error_primitive = bad_primitive;
	ep->dl_errno = DL_BADPRIM;
	ep->dl_unix_errno = 0;
	mp->b_wptr += sizeof(dl_error_ack_t);
	qreply(q, mp);
	return 1;
    }
    }
    return 0;
}

/* ----------------------------------------------------------------------------
 * RX -> DLPI delivery.  SAP demux, own-MAC/broadcast filter, DL_UNITDATA_IND.
 * Adapted from hydra/aen toss_packet_up_stream.  `count` is the full frame length
 * (header + payload); zz9000eth_drain has already range-checked it to
 * [ETH_HEADER_SIZE, MAX_BUFFER_LENGTH], so `length` below is in [0, 1504].
 *
 * `ready` is zz9000eth_rx_want's verdict, taken before the slot was released:
 * bit j set = stream j can take the frame.  The drain releases a frame with a
 * clear bit for a stream that wants it only past a hold guard, so couldnt_put
 * counts exactly those.  The payload in `packet` is present only when some
 * wanting stream is ready; nothing here reads it otherwise.
 *
 * NO TRAILER DECODE -- deliberately.  The stock aen/hydra ancestor decoded the
 * 4.2BSD trailer encapsulation (ethertype 0x1000..0x1010) here; that arm was
 * inherited verbatim and has been DELETED because it was doubly unsafe:
 *   * length = (ethertype - ETHERTYPE_TRAIL) * 512 reaches 8192, and the trailer
 *     count word was then read at packet[ETH_HEADER_SIZE + length + ETH_TYPE_SIZE]
 *     = up to offset 8208 of the 1518-byte rxbuff -- a ~6.7 KB out-of-bounds read;
 *   * the resulting trailn could be NEGATIVE, so allocb(length + trailn) was
 *     smaller than the following bcopy(..., length) -- an mblk heap overflow.
 * It was dead for legitimate traffic (SVR4 IP never negotiates trailers, open()
 * advertises IFF_NOTRAILERS, and bring-up is `ifconfig zz0 ... -trailers`), but
 * it was reachable by any crafted frame off the wire.  Trailer-tagged frames now
 * take the ordinary path: sap stays 0x1000..0x1010, no stream binds those SAPs,
 * so they are counted in rx_no_sap and dropped.  Every other ethertype (IP 0x0800,
 * ARP 0x0806, ...) already took the else-arm, so normal RX is byte-for-byte
 * unchanged.  Do not reintroduce a decode arm.
 * ------------------------------------------------------------------------- */
static void
toss_packet_up_stream(packet, board_index, count, ready)
char *packet;
int board_index, count, ready;
{
    int j, length;
    unsigned short sap;
    zz9000eth_board_t *board = &zz9000eth_board[board_index];
    zz9000eth_info_t *info = &board->zz9000eth_info;
    zz9000eth_t *zp = board->zz9000eth;
    ether_header_t *packet_header = (ether_header_t *)packet;
    int sap_matched = 0;

    sap = packet_header->ether_type;
    board->zz9000eth_status.last_rx_sap = sap;
    if (sap == ETHERTYPE_ARP) board->zz9000eth_status.rx_arp_seen++;
    if (sap == ETHERTYPE_IP)  board->zz9000eth_status.rx_ip_seen++;

    length = count - ETH_HEADER_SIZE;

    for (j = 0; j < ZZ9000ETH_MAXDEV; j++, zp++) {
	mblk_t *mp = (mblk_t *)NULL;

	if (!zp->q)
	    continue;

	if (zp->flags & ZZ9000ETH_RAW_DATA) {
	    if (ready & (1 << j)) {
		if ((mp = allocb(count, BPRI_MED)) != NULL) {
		    mp->b_datap->db_type = M_DATA;
		    bcopy((caddr_t)packet, (caddr_t)mp->b_wptr, count);
		    mp->b_wptr += count;
		    putnext(zp->q, mp);
		} else
		    board->zz9000eth_status.allocbs_failed++;
	    } else
		board->zz9000eth_status.couldnt_put++;
	} else if (zp->sap == sap) {
	    sap_matched = 1;

	    if (bcmp((char *)packet_header->ether_dhost,
		     (char *)info->paddress, ETH_ADDRESS_SIZE) &&
		bcmp((char *)packet_header->ether_dhost,
		     (char *)zz9000eth_bcast, sizeof(zz9000eth_bcast))) {
		board->zz9000eth_status.rx_not_for_us++;
		continue;
	    }

	    if (ready & (1 << j)) {
		if ((mp = allocb(sizeof(union DL_primitives) +
				 (2 * ETH_ADDRESS_SIZE), BPRI_MED)) != NULL &&
		    (mp->b_cont = allocb(length, BPRI_MED)) != NULL) {
		    register dl_unitdata_ind_t *dp =
			(dl_unitdata_ind_t *)mp->b_wptr;

		    dp->dl_primitive = DL_UNITDATA_IND;
		    dp->dl_dest_addr_length = ETH_ADDRESS_SIZE;
		    dp->dl_dest_addr_offset = sizeof(union DL_primitives);
		    dp->dl_src_addr_length = ETH_ADDRESS_SIZE;
		    dp->dl_src_addr_offset =
			sizeof(union DL_primitives) + ETH_ADDRESS_SIZE;
		    dp->dl_reserved = 0;

		    bcopy((caddr_t)&packet_header->ether_dhost,
			  (caddr_t)mp->b_wptr + dp->dl_dest_addr_offset,
			  ETH_ADDRESS_SIZE);
		    bcopy((caddr_t)&packet_header->ether_shost,
			  (caddr_t)mp->b_wptr + dp->dl_src_addr_offset,
			  ETH_ADDRESS_SIZE);
		    mp->b_wptr += sizeof(union DL_primitives) +
			(2 * ETH_ADDRESS_SIZE);
		    mp->b_datap->db_type = M_PROTO;

		    bcopy((caddr_t)&packet[ETH_HEADER_SIZE],
			  (caddr_t)mp->b_cont->b_wptr, length);
		    mp->b_cont->b_wptr += length;
		    mp->b_cont->b_datap->db_type = M_DATA;

		    putnext(zp->q, mp);
		    board->zz9000eth_status.rx_delivered++;
		} else {
		    board->zz9000eth_status.allocbs_failed++;
		    if (mp)
			freemsg(mp);
		}
	    } else
		board->zz9000eth_status.couldnt_put++;
	}
    }

    if (!sap_matched)
	board->zz9000eth_status.rx_no_sap++;
}

/* ----------------------------------------------------------------------------
 * ioctls: SIOC*IFFLAGS ack, board count, config + status snapshots.
 * ------------------------------------------------------------------------- */

/*
 * NAK an M_IOCTL the way the rest of the driver does (M_IOCNAK + errno back up
 * the read side), dropping any data block still attached to it.
 */
static void
zz9000eth_ioc_nak(q, mp, err)
queue_t *q;
mblk_t *mp;
int err;
{
    register struct iocblk *iocbp = (struct iocblk *)mp->b_rptr;

    if (mp->b_cont) {
	freemsg(mp->b_cont);
	mp->b_cont = NULL;
    }
    mp->b_datap->db_type = M_IOCNAK;
    iocbp->ioc_count = 0;
    iocbp->ioc_rval = 0;
    iocbp->ioc_error = err;
    putnext(RD(q), mp);
}

/*
 * Fetch the user-address argument of a TRANSPARENT M_IOCTL out of its b_cont,
 * with the checks stock aen.c omits.  aen (and this driver before this change)
 * did an unconditional
 *	caddr_t arg = *(caddr_t *)mp->b_cont->b_rptr;
 * so a malformed ioctl -- an M_IOCTL with NO b_cont at all, which is exactly what
 * a non-TRANSPARENT I_STR ioctl carrying zero bytes of data looks like -- was a
 * kernel NULL-deref reachable from an unprivileged ioctl() on /dev/zz0.  A
 * b_cont shorter than a pointer would likewise over-read it.
 *
 * Returns 1 with *argp set (0 for the non-TRANSPARENT case, where there is no
 * user address to fetch and the caller does not use it), or 0 => the caller must
 * NAK with EINVAL.
 */
static int
zz9000eth_ioc_arg(mp, iocbp, argp)
mblk_t *mp;
struct iocblk *iocbp;
caddr_t *argp;
{
    *argp = (caddr_t)0;

    if (iocbp->ioc_count != TRANSPARENT)
	return 1;
    if (mp->b_cont == NULL || blklen(mp->b_cont) < (int)sizeof(caddr_t))
	return 0;
    *argp = *(caddr_t *)mp->b_cont->b_rptr;
    return 1;
}

static void
zz9000ethioctl(q, mp)
queue_t *q;
mblk_t *mp;
{
    register struct iocblk *iocbp = (struct iocblk *)mp->b_rptr;
    register zz9000eth_t *zp = (zz9000eth_t *)q->q_ptr;
    register zz9000eth_board_t *board = &zz9000eth_board[zp->board_index];

    /*
     * M_IOCDATA: the stream head's reply to a prior M_COPYOUT/M_COPYIN.  We only
     * ever issue M_COPYOUT (cp_private==0), so just ack the completion -- never
     * fall through to the command switch (its b_cont is NULL after a copyout, so
     * re-reading the user arg would NULL-deref).  Mirrors aen/hydra.
     */
    if (mp->b_datap->db_type == M_IOCDATA) {
	register struct copyresp *csp = (struct copyresp *)mp->b_rptr;
	if (csp->cp_rval) {		/* copyout failed */
	    freemsg(mp);
	    return;
	}
	mp->b_datap->db_type = M_IOCACK;
	freemsg(unlinkb(mp));
	iocbp->ioc_count = 0;
	iocbp->ioc_rval = 0;
	iocbp->ioc_error = 0;
	putnext(RD(q), mp);
	return;
    }

    switch (iocbp->ioc_cmd) {
    case SIOCSIFFLAGS:
    case SIOCGIFFLAGS:
	mp->b_datap->db_type = M_IOCACK;
	putnext(RD(q), mp);
	return;

    case ZZ9000ETH_NUMBER_OF_BOARDS:
    {
	caddr_t arg;

	if (!zz9000eth_ioc_arg(mp, iocbp, &arg)) {
	    zz9000eth_ioc_nak(q, mp, EINVAL);
	    return;
	}
	if (mp->b_cont) {
	    freemsg(mp->b_cont);
	    mp->b_cont = NULL;
	}
	if (!(mp->b_cont = allocb(sizeof(int), BPRI_MED))) {
	    zz9000eth_ioc_nak(q, mp, ENOMEM);
	    return;
	}
	*(int *)mp->b_cont->b_rptr = zz9000eth_number_of_boards;
	mp->b_cont->b_wptr += sizeof(int);
	if (iocbp->ioc_count == TRANSPARENT) {
	    struct copyreq *creq = (struct copyreq *)mp->b_rptr;
	    mp->b_datap->db_type = M_COPYOUT;
	    creq->cq_addr = arg;
	    mp->b_wptr = mp->b_rptr + sizeof *creq;
	    creq->cq_size = sizeof(int);
	    creq->cq_flag = 0;
	    creq->cq_private = (mblk_t *)0;
	} else {
	    iocbp->ioc_count = sizeof(int);
	    mp->b_datap->db_type = M_IOCACK;
	}
	putnext(RD(q), mp);
	return;
    }

    case ZZ9000ETH_GET_STATUS:
    {
	caddr_t arg;
	int s;

	if (!zz9000eth_ioc_arg(mp, iocbp, &arg)) {
	    zz9000eth_ioc_nak(q, mp, EINVAL);
	    return;
	}
	if (mp->b_cont) {
	    freemsg(mp->b_cont);
	    mp->b_cont = NULL;
	}
	if (!(mp->b_cont = allocb(sizeof(zz9000eth_status_t), BPRI_MED))) {
	    zz9000eth_ioc_nak(q, mp, ENOMEM);
	    return;
	}
	/*
	 * M3: one consistent snapshot.  The poll callout bumps these counters
	 * (RX drain, deferred TX, tx_nolink); unmasked, a tick mid-copy yields a
	 * torn record (packets_received and if_ipackets from different ticks).  108 bytes of
	 * moves; allocb stays outside the window.
	 */
	s = splzz9000eth();
	board->zz9000eth_status.if_ipackets = board->ifstats.ifs_ipackets;
	board->zz9000eth_status.if_opackets = board->ifstats.ifs_opackets;
	board->zz9000eth_status.if_ierrors = board->ifstats.ifs_ierrors;
	board->zz9000eth_status.if_oerrors = board->ifstats.ifs_oerrors;
	*(zz9000eth_status_t *)mp->b_cont->b_rptr = board->zz9000eth_status;
	splx(s);
	mp->b_cont->b_wptr += sizeof(zz9000eth_status_t);
	if (iocbp->ioc_count == TRANSPARENT) {
	    struct copyreq *creq = (struct copyreq *)mp->b_rptr;
	    mp->b_datap->db_type = M_COPYOUT;
	    creq->cq_addr = arg;
	    mp->b_wptr = mp->b_rptr + sizeof *creq;
	    creq->cq_size = sizeof(zz9000eth_status_t);
	    creq->cq_flag = 0;
	    creq->cq_private = (mblk_t *)0;
	} else {
	    iocbp->ioc_count = sizeof(zz9000eth_status_t);
	    mp->b_datap->db_type = M_IOCACK;
	}
	putnext(RD(q), mp);
	return;
    }

    /*
     * Declared in zz9000ethuser.h but never implemented.  Given their own case
     * labels rather than left to fall into the default arm so the state is
     * visible at the point a future implementor will look: these are recognised
     * commands with no implementation, not unknown commands.  Behaviour is the
     * same EINVAL NAK either way.
     */
    case ZZ9000ETH_CLEAR_STATUS:
    case ZZ9000ETH_GET_CONFIG:
	zz9000eth_ioc_nak(q, mp, EINVAL);
	return;

    default:
	zz9000eth_ioc_nak(q, mp, EINVAL);
	return;
    }
}

/*
 * Boot init.  Not in the kit's chain (KIT.info init= is empty): the driver
 * looks for the board at the first open (lazy open, like aen), so a kernel
 * with it touches nothing at boot.  Kept for a kernel that wants the probe at
 * boot; it says nothing without a ZZ9000.
 */
void
zz9000ethinit()
{
    zz9000eth_autoconfig();
}


