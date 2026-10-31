/*
 * z3660eth.c -- Z3660 accelerator onboard ethernet for Amix (SVR4 / 68030).
 *
 * The native STREAMS/DLPI ethernet analogue of the z3660.c SCSI driver.  The
 * Z3660 firmware already moves whole ethernet frames between the 68k guest and
 * the Zynq GEM for AmigaOS's SANA-II Z3660Net.device; this driver speaks the
 * SAME firmware mailbox (a tiny MMIO register + two shared DDR frame windows)
 * but presents it to Amix as a DLPI Style-1 connectionless ethernet provider.
 * It is NOT NIC emulation.  The board is the same Zorro III combo board the
 * SCSI/RTG side enumerates (manuf 0x144B, product 0x01).
 *
 * Protocol (board_base-relative, all big-endian MMIO -- see z3660eth.h, verified
 * against z3660-drivers/common/z3660_regs.h + Z3660/src/{ethernet.c,rtg/rtg.c}):
 *   ZZ_CONFIG(0x104)  enable=1 / ack-ETH=24 / disable=0   (mutually exclusive)
 *   ZZ_ETH_TX(0x190)  W byte-length = trigger TX (synchronous ~1ms); R = result
 *   ZZ_ETH_RX(0x194)  W any = advance/free the current RX backlog slot
 *   ZZ_ETH_MAC_HI/LO  the firmware-owned station MAC
 *   ZZ_ETH_RX_ADDR(0x1A4) R = board-relative byte offset of the current RX slot
 *   ZZ_INT_STATUS(0x1A8)  R = pending bits (ETH=1)
 *   TX frame  @ board+0x07EE0000 (bare frame), RX backlog ring @ board+0x07ED0000
 *   (32 usable slots x 2048; each slot = 4-byte [len][serial] header + payload).
 *
 * RX delivery: the firmware raises the receive interrupt as Amiga INT6/EXTER,
 * for which Amix has NO driver hook (only int2_tbl, walked at level 2).  So this
 * driver DRAINS the backlog from a clock-level timeout() poll callout by default
 * (z3660eth_poll -> z3660eth_drain).  z3660ethintr() is provided for a future
 * interrupt-driven path (int2_tbl via board-mod, or a p6int/int6_tbl change) but
 * is not wired into kernel.c.  TX is synchronous and needs no interrupt.
 *
 * Cache coherency, 030 line: the eth windows are CACHEABLE (AMIX section 0 is
 * mapped with cache-inhibit clear -- see the mapping note above z3660eth_map())
 * and per-page cache-inhibit is impossible whichever way the board is mapped:
 * THIS kernel has no PG_CI bit at all (immu.h:68-75 defines only PG_ADDR/PG_LOCK/
 * PG_M/PG_REF/PG_W/PG_V), so an sptalloc'd mapping is no less cacheable.
 *
 * That is an 030 fact and it does NOT generalise.  The 040/060 kernel writes a
 * two-bit CM field in its leaf PTEs (bits 6-5, hat_cm_ram -- copyback since
 * 2026-07-30), and on that line DTT0 = 0x003fc060 makes the whole low 1 GB --
 * every window the direct-map arm touches -- cache-INHIBITED for free, while the
 * sptalloc arm would take hat_cm_ram and be unusable.  The two mapping arms are
 * therefore NOT cache-equivalent there.  See docs/BLIZZARD-F3.md.
 *
 * Line-granular 040/060 maintenance and the attach-time free-ride assert live
 * below, gated by z3660eth_cache (0 = off, the shipping default; 40/60 poked
 * through /dev/kmem).  On the 030 line nothing of it is reachable, which is the
 * proven aen/z3660 no-flush behaviour, correct there because the 030 data cache
 * is effectively off in every validated config.
 * See docs/BLIZZARD-F3.md and ETHERNET-SCOPING.md sections 5/10.
 *
 * Detection mirrors z3660.c exactly: autocon(0x144B0001) first; on a miss, and
 * only on AGA (VPOSR>=0x22, so the ECS build box never touches it), probe the
 * fixed combo base 0x10000000 and verify the firmware MAC register answers.  On
 * the WinUAE build box (which emulates an a2065, not the Z3660 GEM) detection
 * misses and open() returns ENXIO -- the compile/link/boot/register gate.
 *
 * Refs: hydra-amix aen/aen.c + hydra/hydra.c (STREAMS/DLPI skeleton),
 *       amix-z3660scsi/src/z3660.c (Z3660 mailbox/sptalloc idiom), ETHERNET-SCOPING.md.
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
#include "z3660eth.h"

extern int	autocon();
extern caddr_t	sptalloc();
extern void	sptfree();
extern int	timeout();
extern void	bcopy(), bzero();

#ifndef min
#define min(a,b)	((a) < (b) ? (a) : (b))
#endif

extern char *panicstr;

void z3660eth_autoconfig();
void z3660ethinit();
void z3660ethintr();
static void z3660eth_poll();
static void z3660eth_drain();
static void z3660eth_tx_drain();
static void toss_packet_up_stream();
static int  z3660ethopen(), z3660ethclose(), z3660ethwput(), z3660ethwsrv();
static void z3660ethioctl(), z3660eth_ioc_nak();
static int  z3660eth_ioc_arg();
static int  z3660ethproto(), z3660ethxmit();
static int  z3660eth_map(), z3660eth_initialize();
static void z3660eth_unmap();
static int  z3660eth_absent();

static struct module_info z3660eth_minfo =
{
    0x7A65, "zen", Z3660ETH_MIN, Z3660ETH_MAX, Z3660ETH_HIWAT, Z3660ETH_LOWAT,
};

static struct qinit z3660ethrinit =
{
    NULL, NULL, z3660ethopen, z3660ethclose, NULL, &z3660eth_minfo, NULL,
};

static struct qinit z3660ethwinit =
{
    z3660ethwput, z3660ethwsrv, NULL, NULL, NULL, &z3660eth_minfo, NULL,
};

struct streamtab z3660ethinfo =		/* goes in cdevsw[].d_str */
{
    &z3660ethrinit, &z3660ethwinit, NULL, NULL,
};

extern struct ifstats *ifstats;

static z3660eth_board_t z3660eth_board[Z3660ETH_MAXBOARDS];
static int z3660eth_number_of_boards;
static int z3660eth_autoconfigured;

/* diagnostics (read via /dev/mem on real HW, like z3660.c) */
unsigned long z3660eth_board_phys, z3660eth_last_rc, z3660eth_last_off, z3660eth_machi, z3660eth_maclo;
unsigned char z3660eth_present;
unsigned char z3660eth_direct_map;	/* 1 = section-0 identity, 0 = sptalloc'd */

/*
 * AB-04 (2026-09-27): a failed probe is remembered per board (info->map_verdict)
 * so later opens neither map nor probe again, and it prints ONE console line.
 * z3660eth_reprobe is the re-probe knob: poke it nonzero through /dev/kmem and
 * the next open forgets one cached verdict and probes afresh (the knob clears
 * itself).  For a firmware whose ethernet comes up after the first open; there
 * is no Zorro hot-plug.  The counters are diagnostics for /dev/kmem readers.
 */
long z3660eth_reprobe;
unsigned long z3660eth_map_fails;	/* probes that ended in a failure verdict */
unsigned long z3660eth_spt_freed;	/* kernel-VA pages returned by sptfree() */

#define WRLONG(base,off,val) (*(volatile unsigned long *)((base) + (off)) = (unsigned long)(val))
#define RDLONG(base,off)     (*(volatile unsigned long *)((base) + (off)))

/* ----------------------------------------------------------------------------
 * BLIZZARD F3-M0 -- the 040/060 gate, and the free ride asserted instead of
 * silently depended on.  Full argument in docs/BLIZZARD-F3.md.
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
long z3660eth_cache;		/* 0 = off (default), 40 = 68040 DC, 60 = 68060 DC */
long z3660eth_ci_enforce;	/* 0 = warn on a failed free-ride check, else refuse */

/* attach-time capture + verdict, all kmem-readable (docs/BLIZZARD-F3.md 7) */
unsigned long z3660eth_dtt0, z3660eth_dtt1, z3660eth_cacr;
unsigned long z3660eth_ci_ok;		/* 0 = not checked, 1 = verified, 2 = FAILED */
unsigned long z3660eth_maint_on;	/* 1 = the four conjuncts hold; line ops armed */
unsigned long z3660eth_sptalloc_unsafe;	/* sptalloc arm taken with a live data cache */

/*
 * 040/060 control-register reads, as raw .word so the -m68020 assembler accepts
 * them, with the destination pinned by an explicit register variable.  Verified
 * against this toolchain (gcc 2.7.2.3 / GNU as 2.8.1) by assembling the mnemonic
 * and disassembling the word, both directions:
 *   4e7a 0006 = movec %dtt0,%d0   4e7a 0007 = movec %dtt1,%d0
 *   4e7a 0002 = movec %cacr,%d0
 * Every caller is gated on z3660eth_cache != 0, so none of this is reachable on
 * the 68030 line, where these words would fault.
 */
static unsigned long
z3660eth_rd_dtt0()
{
    register unsigned long v __asm__("d0");

    __asm__ __volatile__(".word 0x4e7a,0x0006" : "=d" (v));
    return v;
}

static unsigned long
z3660eth_rd_dtt1()
{
    register unsigned long v __asm__("d0");

    __asm__ __volatile__(".word 0x4e7a,0x0007" : "=d" (v));
    return v;
}

static unsigned long
z3660eth_rd_cacr()
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
z3660eth_ttr_class(t, a)
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
z3660eth_ci_span(lo, hi)
unsigned long lo, hi;			/* lo inclusive, hi exclusive */
{
    unsigned long a, last;
    int bad = 0, c0, c1;

    a = lo;
    last = (hi - 1) & ~0x00FFFFFFL;
    for (;;) {
	c0 = z3660eth_ttr_class(z3660eth_dtt0, a);
	c1 = z3660eth_ttr_class(z3660eth_dtt1, a);
	if (c0 == 2 || c1 == 2 || (c0 == 0 && c1 == 0)) {
	    bad = 1;
	    cmn_err(CE_WARN,
		"zen: VA 0x%x is NOT cache-inhibited (dtt0 0x%x dtt1 0x%x)",
		a, z3660eth_dtt0, z3660eth_dtt1);
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
 * only when z3660eth_ci_enforce is set, because a first-silicon boot that
 * refuses its own network device tells you nothing.
 */
int
z3660eth_ci_check(board_index)
int board_index;
{
    z3660eth_info_t *info = &z3660eth_board[board_index].z3660eth_info;
    unsigned long base = (unsigned long)info->board_base;
    int bad;

    z3660eth_ci_ok = 0;
    z3660eth_maint_on = 0;
    if (z3660eth_cache == 0)		/* emulated CPU: nothing to check, nothing to do */
	return 0;

    z3660eth_dtt0 = z3660eth_rd_dtt0();
    z3660eth_dtt1 = z3660eth_rd_dtt1();
    z3660eth_cacr = z3660eth_rd_cacr();

    bad  = z3660eth_ci_span(base, base + ZZ_REGS_BYTES);
    bad |= z3660eth_ci_span(base + ZZ_RXBACKLOG_OFF, base + ZZ_WINDOW_TOP);

    if (z3660eth_direct_map == 0) {
	z3660eth_sptalloc_unsafe++;
	bad = 1;
	cmn_err(CE_WARN,
	    "zen: sptalloc mapping arm is unsupported with a live data cache");
    } else if ((unsigned long)info->regs != base ||
	       (unsigned long)info->frame != base + ZZ_RXBACKLOG_OFF) {
	bad = 1;
	cmn_err(CE_WARN,
	    "zen: phystokv is not the identity (base 0x%x regs 0x%x frame 0x%x)",
	    base, (unsigned long)info->regs, (unsigned long)info->frame);
    }

    if (bad == 0) {
	z3660eth_ci_ok = 1;
	z3660eth_maint_on = 1;
	return 0;
    }
    z3660eth_ci_ok = 2;
    cmn_err(CE_WARN, "zen: cache-inhibit assertion FAILED (cache=%d cacr 0x%x)",
	(int)z3660eth_cache, z3660eth_cacr);
    return z3660eth_ci_enforce ? ENXIO : 0;
}

/* ----------------------------------------------------------------------------
 * BLIZZARD F3-M2 -- 68040/68060 data-cache maintenance for the shared eth DDR
 * windows.  Replaces a block that was #ifdef'd behind a -D nobody passed and was
 * wrong four ways when read (docs/BLIZZARD-F3.md 3).  No #ifdef now: compiled in
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
unsigned long z3660eth_push_n, z3660eth_inv_n;		/* line-op invocations   */
unsigned long z3660eth_push_bytes, z3660eth_inv_bytes;	/* bytes, pre-rounding   */
unsigned long z3660eth_sync_n;				/* barriers executed     */
unsigned long z3660eth_range_ovf;			/* impossible range refused */

#define Z3660ETH_CPUSHL(p) __asm__ __volatile__(".word 0xf468" : : "a" (p) : "memory")
#define Z3660ETH_CINVL(p)  __asm__ __volatile__(".word 0xf448" : : "a" (p) : "memory")
#define Z3660ETH_NOP()	   __asm__ __volatile__(".word 0x4e71" : : : "memory")
#define Z3660ETH_LINEPTR   register unsigned char *p __asm__("a0")

/*
 * Push-and-invalidate every line covering [va, va+len).  `va' is a kernel VA
 * that has been PROVEN equal to the physical address (z3660eth_ci_check), which
 * is what the line ops select by; z3660eth_maint_on is 0 whenever that proof is
 * unavailable, so nothing here ever fires at an address it did not verify.
 */
static void
z3660eth_push(va, len)
unsigned long va, len;
{
    Z3660ETH_LINEPTR;
    unsigned long end;

    if (z3660eth_cache == 0 || z3660eth_maint_on == 0 || len == 0)
	return;
    if (va + len < va) {			/* wrap: refuse rather than loop wild */
	z3660eth_range_ovf++;
	return;
    }
    p = (unsigned char *)(va & ~15L);
    end = (va + len + 15) & ~15L;
    while ((unsigned long)p < end) {
	Z3660ETH_CPUSHL(p);
	Z3660ETH_CINVL(p);
	p += 16;
    }
    z3660eth_push_n++;
    z3660eth_push_bytes += len;
}

/*
 * Invalidate every line covering [va, va+len) -- RX ring only, where nothing the
 * CPU wrote can live (see the block comment above).
 */
static void
z3660eth_inval(va, len)
unsigned long va, len;
{
    Z3660ETH_LINEPTR;
    unsigned long end;

    if (z3660eth_cache == 0 || z3660eth_maint_on == 0 || len == 0)
	return;
    if (va + len < va) {
	z3660eth_range_ovf++;
	return;
    }
    p = (unsigned char *)(va & ~15L);
    end = (va + len + 15) & ~15L;
    while ((unsigned long)p < end) {
	Z3660ETH_CINVL(p);
	p += 16;
    }
    z3660eth_inv_n++;
    z3660eth_inv_bytes += len;
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
 * NOT gated on z3660eth_maint_on: it takes no address, so the identity proof the
 * line ops need does not apply to it.
 *
 * THAT `nop' SERIALIZES PENDING WRITES ON THE 68060 IS ASSUMED, NOT MEASURED --
 * owed a read of the MC68060UM before the store-buffer rung, on the discipline
 * that made the port lane settle PCR bit 1 from the manual before any PCR code.
 */
static void
z3660eth_sync()
{
    if (z3660eth_cache == 0)
	return;
    Z3660ETH_NOP();
    z3660eth_sync_n++;
}

/* ----------------------------------------------------------------------------
 * Discovery + mapping.  Fills board->z3660eth_info {board_base, regs, frame,
 * txwin, paddress}.  0 on success; ENXIO when no Z3660 is present; ENOMEM on a
 * mapping failure.  Same AGA-gated discovery as z3660map() in z3660.c, but it
 * verifies via the firmware MAC register instead of the SCSI mailbox.
 *
 * MAPPING STRATEGY -- why the metal box needs no sptalloc() at all.
 *
 * AMIX's supervisor root table has FOUR level-A entries, one per 1 GB section
 * (tc_on encodes TIA=2; the table is built in ml/exp pstart()).  Entry 0, which
 * covers VA 0x00000000-0x3FFFFFFF, is a single EARLY-TERMINATION page descriptor
 * whose page address is 0 -- i.e. a straight identity map of the whole low 1 GB,
 * RAM or not.  That is why immu.h:348 can define phystokv(p) as (p), and why
 * every stock Amiga driver simply dereferences the address autocon() gave it
 * (the custom chips at 0xDFFxxx and the A3000 SCSI at 0xDD0000 are far above
 * physical RAM and are reached exactly this way -- this driver's own AGA gate
 * reads VPOSR = 0xDFF004 before it has mapped anything).  Only section 1
 * (VSECT1 = 0x40000000, the kernel-virtual arena) is page-table-backed, and
 * section 1 is what sptalloc() hands addresses out of.
 *
 * Note this is NOT the 68030 transparent-translation registers: amiga/boot/
 * copyit.s:87-88 pmoves ZERO into %tt0/%tt1, and ttrap.s:326-329's "tt0_on"
 * word 0x003F0143 has E=0 -- its own comment says "Disable".  TT is off in AMIX;
 * the low-1 GB identity map is a page-table early termination.  Same 0x40000000
 * boundary, different mechanism.
 *
 * So when the board sits below VSECT1 -- the shipped Z3660 config, autoconfig_rtg
 * NO, fixed base 0x10000000 with the frame windows at 0x17ED0000/0x17EE0000 --
 * its physical address ALREADY IS a valid supervisor VA and no mapping call is
 * needed.  Taking that path is not cosmetic: sptalloc() draws from sptmap, a
 * HARDCODED 2048-page (4 MB) resource map that page[] -- sized by presented RAM
 * -- is carved out of first.  Past ~83 MB of RAM no 64-page contiguous run was
 * left, the frame sptalloc() returned 0, and this function reported ENOMEM,
 * printing "no Z3660 ethernet found" for a board that was perfectly visible and
 * whose SCSI half was at that moment serving the root filesystem.  Mapping
 * directly removes zen0 from that allocation budget entirely.
 *
 * sptalloc() is kept for a board at or above VSECT1 -- autoconfig_rtg YES puts
 * the combo window at 0x40000000, inside the page-table-backed section, exactly
 * the case a4091.c:152-153 handles.  The direct path is chosen only when the
 * ENTIRE span the driver touches (base .. base+ZZ_WINDOW_TOP) stays below
 * VSECT1, so a board based just under the boundary still takes the safe path.
 * Cache semantics are identical either way ON THE 030 LINE ONLY -- on the
 * 040/060 line the arms differ and only the direct one is supported (file
 * header; docs/BLIZZARD-F3.md).
 * ------------------------------------------------------------------------- */
static int
z3660eth_map(board_index)
int board_index;
{
    z3660eth_board_t *board = &z3660eth_board[board_index];
    z3660eth_info_t *info = &board->z3660eth_info;
    long base, size;
    unsigned long hi, lo, bp;
    int e;

    if (info->regs)
	return 0;
    if (info->map_verdict) {		/* AB-04: probed before and failed */
	if (!z3660eth_reprobe)
	    return info->map_verdict;
	z3660eth_reprobe = 0;
	info->map_verdict = 0;
    }

    if (!autocon(Z3660_PROD, board_index, &base, &size)) {
	if (board_index != 0)
	    return z3660eth_absent(board_index, ENXIO, "no board", 0);
	if ((((*(volatile unsigned short *)VPOSR) >> 8) & 0x7F) < 0x22)
	    /* ECS/OCS build box -- no Z3660 here */
	    return z3660eth_absent(board_index, ENXIO, "not AGA", 0);
	base = Z3660_FIXED;		/* AGA: probe the fixed combo window */
    }

    info->board_base = base;
    bp = (unsigned long)base;
    z3660eth_board_phys = bp;

    /*
     * Whole window inside the identity-mapped section 0?  Then the physical
     * address is the kernel VA.  The subtraction cannot wrap: bp < VSECT1.
     */
    if (bp < (unsigned long)VSECT1 &&
	((unsigned long)VSECT1 - bp) >= (unsigned long)ZZ_WINDOW_TOP) {
	z3660eth_direct_map = 1;
	info->spt_mapped = 0;
	info->regs  = (volatile unsigned char *)phystokv((paddr_t)base);
	info->frame = (volatile unsigned char *)
			phystokv((paddr_t)base + ZZ_RXBACKLOG_OFF);
    } else {
	z3660eth_direct_map = 0;
	info->spt_mapped = 1;		/* from here every failure must unmap */
	info->regs = (volatile unsigned char *)sptalloc(ZZ_REGS_PAGES, PG_V,
			phystopfn((paddr_t)base), 0);
	info->frame = (volatile unsigned char *)sptalloc(ZZ_FRAME_PAGES, PG_V,
			phystopfn((paddr_t)base + ZZ_RXBACKLOG_OFF), 0);
    }
    if (info->regs == 0 || info->frame == 0) {
	/*
	 * sptalloc failure (sptmap exhausted), or a base of 0.  Free whichever
	 * half DID map.  Not cached: VA pressure is transient, so the next open
	 * tries again -- which is safe now that nothing is kept on failure.
	 */
	z3660eth_unmap(info);
	z3660eth_map_fails++;
	if (!info->nomem_said) {
	    info->nomem_said = 1;
	    cmn_err(CE_NOTE,
		"zen: no Z3660 ethernet found (kernel VA map failed); will retry");
	}
	return ENOMEM;
    }
    info->txwin = info->frame + ZZ_TXWIN_OFF;

    /*
     * F3-M0: assert the free ride BEFORE the first register read, so a stale
     * cache line cannot be mistaken for a firmware answer.  A no-op with
     * z3660eth_cache == 0, which is every shipping image.
     */
    if ((e = z3660eth_ci_check(board_index)) != 0)
	return z3660eth_absent(board_index, e, "cache-inhibit check refused",
			       0);

    /* verify the firmware ethernet register file answers (open bus => absent) */
    hi = RDLONG(info->regs, ZZ_ETH_MAC_HI);
    lo = RDLONG(info->regs, ZZ_ETH_MAC_LO);
    z3660eth_machi = hi;
    z3660eth_maclo = lo;
    if (hi == 0xFFFFFFFF || (hi & 0xFFFF) == 0)	/* open bus / not a Z3660 window */
	return z3660eth_absent(board_index, ENXIO, "MAC reads", 1);

    info->paddress[0] = (unsigned char)(hi >> 8);
    info->paddress[1] = (unsigned char)hi;
    info->paddress[2] = (unsigned char)(lo >> 24);
    info->paddress[3] = (unsigned char)(lo >> 16);
    info->paddress[4] = (unsigned char)(lo >> 8);
    info->paddress[5] = (unsigned char)lo;

    z3660eth_present = 1;
    return 0;
}

/*
 * Undo z3660eth_map()'s mapping.  THE RULE: map, probe, free on failure.  The
 * sptalloc arm draws from sptmap, a fixed kernel-VA resource map that nothing
 * else returns pages to; before AB-04 every failed probe kept 1 + ZZ_FRAME_PAGES
 * pages (33 at NBPP 4096, 65 at 2048); on the AB-04 rig S99zen's 16 boot-time
 * opens left 18 free sptmap pages (549 without S99zen), so raw I/O and big kmem
 * pools slept forever on Km_BigWanted.
 * flag 0 = unmap only: these are board pages, never page[] frames to release.
 * The direct-map arm is an identity VA with nothing to give back.  close() never
 * unmaps (a mapped board stays mapped for the kernel's life, and map() returns
 * early on it), so this is the only teardown path.
 */
static void
z3660eth_unmap(info)
z3660eth_info_t *info;
{
    if (info->spt_mapped) {
	if (info->regs) {
	    sptfree((caddr_t)info->regs, ZZ_REGS_PAGES, 0);
	    z3660eth_spt_freed += ZZ_REGS_PAGES;
	}
	if (info->frame) {
	    sptfree((caddr_t)info->frame, ZZ_FRAME_PAGES, 0);
	    z3660eth_spt_freed += ZZ_FRAME_PAGES;
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
 * The line keeps the "zen: no Z3660 ethernet found" prefix bench logs grep for.
 * showmac: append the two MAC register words the probe read.
 */
static int
z3660eth_absent(board_index, e, why, showmac)
int board_index, e;
char *why;
int showmac;
{
    z3660eth_info_t *info = &z3660eth_board[board_index].z3660eth_info;

    z3660eth_unmap(info);
    z3660eth_present = 0;
    z3660eth_map_fails++;
    info->map_verdict = e;
    if (showmac)
	cmn_err(CE_NOTE,
	    "zen: no Z3660 ethernet found (%s 0x%x 0x%x); opens fail errno %d",
	    why, z3660eth_machi, z3660eth_maclo, e);
    else
	cmn_err(CE_NOTE,
	    "zen: no Z3660 ethernet found (%s); opens fail errno %d", why, e);
    return e;
}

void
z3660eth_autoconfig()
{
    if (z3660eth_autoconfigured)
	return;
    z3660eth_autoconfigured = 1;
    z3660eth_number_of_boards = 0;

    /* a failure prints its own single line from z3660eth_map() */
    if (z3660eth_map(0) == 0)
	z3660eth_number_of_boards = 1;
}

/*
 * Bring a board up: ensure mapped, init the per-stream array, enable firmware
 * eth interrupt delivery, and start the RX poll callout.  Returns 0, or the
 * errno z3660eth_map() gave (ENXIO absent -- cached; ENOMEM no kernel VA).
 */
static int
z3660eth_initialize(board_index)
int board_index;
{
    z3660eth_board_t *board = &z3660eth_board[board_index];
    z3660eth_info_t *info = &board->z3660eth_info;
    z3660eth_t *zp;
    int n, e;

    if ((e = z3660eth_map(board_index)) != 0)
	return e;

    for (n = 0, zp = board->z3660eth; n < Z3660ETH_MAXDEV; ++n, ++zp) {
	zp->q = (queue_t *)NULL;
	zp->sap = 0;
	zp->state = DL_UNBOUND;
    }

    info->last_serial = 0;		/* firmware's first serial is 1 */
    info->tx_busy = 0;

    bzero((caddr_t)&board->z3660eth_status, sizeof(board->z3660eth_status));
    board->z3660eth_status.board_state = Z3660ETH_BOARD_RUNNING;

    /*
     * CRITICAL: do NOT enable the firmware eth INT6 (write 0/DISABLE, not 1).
     * AMIX has NO level-6 eth handler.  The firmware raises INT6 on every received
     * frame (rtg.c:336, gated by interrupt_enabled_ethernet = CONFIG bit0); AMIX's
     * p6int->aciabintr runs but never acks the eth source, so INT6 stays asserted
     * and the level-6 interrupt STORMS -> hard-lock.  (Diagnosed on real HW
     * 2026-06-21 from the serial [PC] heartbeat: locked in p6int/aciabintr/intret
     * as the LAN's ARP broadcasts flooded RX.)  We POLL the backlog, and RX fills
     * it regardless of this flag (ethernet.c only gates a debug print on it), so
     * writing 0 keeps the full datapath AND clears any pending INT6 (rtg.c:1168-9).
     * Interrupt-driven RX is a future board-mod / int6_tbl option (ETHERNET-SCOPING Q1).
     */
    WRLONG(info->regs, ZZ_CONFIG, ZZ_CONFIG_DISABLE);

    if (!info->poll_running) {
	info->poll_running = 1;
	info->poll_id = timeout(z3660eth_poll, (caddr_t)(long)board_index,
				Z3660ETH_POLL_TICKS);
    }
    return 0;
}

/* ----------------------------------------------------------------------------
 * STREAMS open/close -- clone/dev_t machinery copied from hydra/aen (the double
 * open under /dev/ip and /dev/arp depends on the makedevice() minor rewrite).
 * ------------------------------------------------------------------------- */
static int
z3660ethopen(q, devp, flag, sflag, credp)
register queue_t *q;
dev_t *devp;
int flag, sflag;
cred_t *credp;
{
    register z3660eth_t *zp;
    register int minor_device, board_index, sap_index;
    z3660eth_board_t *board;
    int s, e;

    if (sflag == MODOPEN || sflag == CLONEOPEN)
	return EINVAL;

    z3660eth_autoconfig();

    minor_device = getminor(*devp);
    board_index = minor_device & 0x0F;
    sap_index = (minor_device & 0xF0) >> 4;

    if (board_index >= Z3660ETH_MAXBOARDS)
	return ENODEV;
    board = &z3660eth_board[board_index];

    if (board->z3660eth_status.board_state != Z3660ETH_BOARD_RUNNING) {
	/* no Z3660 present (e.g. WinUAE build box): ENXIO, cached, silent */
	if ((e = z3660eth_initialize(board_index)) != 0)
	    return e;
    }
    if (board->z3660eth_info.regs == 0)
	return ENXIO;

    if (sap_index == 0) {
	for (sap_index = 0, zp = board->z3660eth;
	     sap_index < Z3660ETH_MAXDEV;
	     sap_index++, zp++)
	    if (!zp->q)
		break;
    } else
	--sap_index;

    if (sap_index >= Z3660ETH_MAXDEV)
	return ENOSPC;

    *devp = makedevice(getmajor(*devp), (((sap_index + 1) << 4) | board_index));

    zp = &board->z3660eth[sap_index];

    /*
     * M2: publish the slot atomically w.r.t. the poll callout, whose RX demux
     * (toss_packet_up_stream) routes on zp->q, flags and sap.  close() clears
     * only zp->q, so without the mask a tick between `zp->q = q' and the field
     * resets would deliver frames up the new stream under the PREVIOUS
     * occupant's sap / RAW flag.  zp->q is still stored last.
     */
    s = splz3660eth();
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
	board->ifstats.ifs_name = "zen";
	board->ifstats.ifs_mtu = Z3660ETH_MTU;
	board->ifstats.ifs_next = ifstats;
	ifstats = &board->ifstats;
    }
    board->ifstats.ifs_unit = board_index;
    board->ifstats.ifs_active = 1;
    board->if_flags = IFF_BROADCAST | IFF_NOTRAILERS | IFF_RUNNING;

    return 0;
}

static int
z3660ethclose(q)
register queue_t *q;
{
    register z3660eth_t *zp = (z3660eth_t *)q->q_ptr;
    register z3660eth_board_t *board = &z3660eth_board[zp->board_index];
    z3660eth_info_t *info = &board->z3660eth_info;
    int dev;

    zp->q = 0;
    OTHERQ(q)->q_ptr = NULL;
    q->q_ptr = NULL;

    for (dev = 0, zp = board->z3660eth; dev < Z3660ETH_MAXDEV; ++dev, ++zp)
	if (zp->q)
	    break;

    if (dev >= Z3660ETH_MAXDEV) {		/* last stream closed */
	if (info->poll_running) {
	    info->poll_running = 0;
	    untimeout(info->poll_id);
	}
	if (info->regs)
	    WRLONG(info->regs, ZZ_CONFIG, ZZ_CONFIG_DISABLE);
	board->z3660eth_status.board_state = Z3660ETH_BOARD_RESET;
	board->ifstats.ifs_active = 0;
    }
    return 0;
}

static int
z3660ethwput(q, mp)
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
	 * sections that race the poll callout mask themselves (NOTES.md).
	 */
	(void)z3660ethproto(q, mp);
	break;

    case M_IOCTL:
    case M_IOCDATA:
	z3660ethioctl(q, mp);
	break;

    default:
	freemsg(mp);
	break;
    }
    return 0;
}

static int
z3660ethwsrv(q)
queue_t *q;
{
    mblk_t *mp;

    /* drain anything deferred by xmit; no TX-completion IRQ exists */
    while ((mp = getq(q)) != NULL) {
	if (!z3660ethxmit(q, mp)) {
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
 * across the ~1ms synchronous trigger (per ETHERNET-SCOPING.md Q2/Q7).  Only the
 * claim (test-and-set) is masked; the release is one aligned store of a volatile
 * int and needs no mask.  See NOTES.md "Interrupt masking", pair M1.
 */
static int
z3660ethxmit(q, mp)
queue_t *q;
mblk_t *mp;
{
    register z3660eth_t *zp = (z3660eth_t *)q->q_ptr;
    z3660eth_board_t *board = &z3660eth_board[zp->board_index];
    z3660eth_info_t *info = &board->z3660eth_info;
    dl_unitdata_req_t *dp = (dl_unitdata_req_t *)mp->b_rptr;
    static unsigned char tmp[ETH_MAXPACKET + 2];
    unsigned char *ap;
    mblk_t *m;
    int s, n, pktsize;
    unsigned long rc;

    s = splz3660eth();			/* M1: atomic claim of txwin + tmp[] */
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
    board->z3660eth_status.last_tx_sap = zp->sap;

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
     * rounded range.  (docs/BLIZZARD-F3.md 5.1)
     */
    z3660eth_push((unsigned long)info->txwin, (unsigned long)pktsize);
    z3660eth_sync();

    /* trigger: synchronous, ~1ms -- NOT at raised spl */
    WRLONG(info->regs, ZZ_ETH_TX, pktsize);
    z3660eth_sync();		/* the result load must not pass the trigger store */
    rc = RDLONG(info->regs, ZZ_ETH_TX);
    z3660eth_last_rc = rc;

    info->tx_busy = 0;			/* release: volatile store, after the rc load */

    if (rc == 0) {
	board->z3660eth_status.packets_sent++;
	board->ifstats.ifs_opackets++;
	if (zp->sap == ETHERTYPE_ARP) board->z3660eth_status.tx_arp++;
	if (zp->sap == ETHERTYPE_IP)  board->z3660eth_status.tx_ip++;
    } else if (rc == 1) {
	board->z3660eth_status.tx_nolink++;	/* no carrier yet; frame dropped */
    } else {
	board->z3660eth_status.tx_errors++;
	board->ifstats.ifs_oerrors++;
    }
    return 1;
}

/* ----------------------------------------------------------------------------
 * RX backlog drain.  Re-reads ZZ_ETH_RX_ADDR every iteration (the cursor only
 * advances on the ZZ_ETH_RX strobe), breaks on the serial sentinel, strobes to
 * free each slot.  Callable from the poll callout OR a future ISR.
 * ------------------------------------------------------------------------- */
static void
z3660eth_drain(board_index)
int board_index;
{
    z3660eth_board_t *board = &z3660eth_board[board_index];
    z3660eth_info_t *info = &board->z3660eth_info;
    static unsigned char rxbuff[MAX_BUFFER_LENGTH];
    volatile unsigned char *slot;
    unsigned long off;
    unsigned short serial;
    int len, loop = 0;

    if (info->regs == 0)
	return;
    board->z3660eth_status.drain_calls++;

    while (++loop <= 256) {
	off = RDLONG(info->regs, ZZ_ETH_RX_ADDR);
	z3660eth_last_off = off;

	if (off < ZZ_RXBACKLOG_OFF ||
	    off >= ZZ_RXBACKLOG_OFF + ZZ_RX_MAX_SLOTS * ZZ_FRAME_SIZE) {
	    board->z3660eth_status.rx_overrun++;
	    break;
	}
	slot = info->frame + (off - ZZ_RXBACKLOG_OFF);

	/*
	 * Invalidate the header line before reading it, every iteration.  The
	 * firmware publishes the serial LAST (ethernet.c: memcpy payload, then
	 * len at +0..1, then serial at +2..3) and rewrites it again at strobe
	 * time when the backlog wraps -- so the header may never be read from a
	 * line that survived a strobe.  `slot' needs no barrier against the
	 * cursor read above: it is COMPUTED from it, so the address does not
	 * exist until that load has returned.
	 */
	z3660eth_inval((unsigned long)slot, (unsigned long)ZZ_RX_FRAME_PAD);
	serial = (slot[2] << 8) | slot[3];
	if (serial == info->last_serial)
	    break;			/* ring drained */

	len = (slot[0] << 8) | slot[1];
	board->z3660eth_status.last_serial = serial;
	board->z3660eth_status.last_rx_len = len;

	if (len < ETH_HEADER_SIZE || len > MAX_BUFFER_LENGTH) {
	    board->z3660eth_status.rx_bad_size++;
	    board->z3660eth_status.rx_errors++;
	    board->ifstats.ifs_ierrors++;
	    info->last_serial = serial;
	    WRLONG(info->regs, ZZ_ETH_RX, 1);
	    z3660eth_sync();
	    continue;				/* payload never read: no invalidate */
	}

	/*
	 * Only now, AFTER the serial has certified the payload, invalidate it.
	 * The outward 16-byte rounding covers the header line (already consumed)
	 * and the slot's unused tail -- both device-written, never CPU-written,
	 * so the invalidate can discard nothing.  rxbuff[] and the mblk it feeds
	 * are NOT maintained: single alias, CPU only, never seen by the device.
	 */
	z3660eth_inval((unsigned long)(slot + ZZ_RX_FRAME_PAD), (unsigned long)len);
	bcopy((caddr_t)(slot + ZZ_RX_FRAME_PAD), (caddr_t)rxbuff, len);

	info->last_serial = serial;
	WRLONG(info->regs, ZZ_ETH_RX, 1);	/* advance/free the slot */
	z3660eth_sync();		/* next cursor read must not pass the strobe */

	board->z3660eth_status.packets_received++;
	board->ifstats.ifs_ipackets++;

	/*
	 * NOTE (Q4, ETHERNET-SCOPING.md): `len` is the firmware-reported frame
	 * length.  Whether it includes the 4-byte FCS is real-HW-confirmable; if
	 * a one-frame probe shows trailing FCS, subtract ETH_CRC_LEN here.  The
	 * scoping verdict is "do NOT subtract by default" -- delivered as-is.
	 */
	toss_packet_up_stream(rxbuff, board_index, len);
    }
}

/* The clock-level poll callout: drain RX, then any deferred TX, then re-arm. */
static void
z3660eth_poll(arg)
caddr_t arg;
{
    int board_index = (int)(long)arg;
    z3660eth_board_t *board = &z3660eth_board[board_index];
    z3660eth_info_t *info = &board->z3660eth_info;

    if (panicstr)
	return;

    if (board->z3660eth_status.board_state == Z3660ETH_BOARD_RUNNING) {
	z3660eth_drain(board_index);
	z3660eth_tx_drain(board_index);
    }
    if (info->poll_running)
	info->poll_id = timeout(z3660eth_poll, arg, Z3660ETH_POLL_TICKS);
}

/*
 * Drain frames deferred by xmit (txwin-busy).  Mirrors hydra transmit_interrupt.
 *
 * A stream that cannot transmit right now ends ITS drain (break), not the whole
 * sweep: `return` here -- the inherited hydra shape -- starved every stream after
 * the first blocked one for a full poll tick, even though the blocking condition
 * (info->tx_busy, or regs == 0) is per-board and the next stream would simply
 * defer too.  The cost of continuing is one getq/putbq pair per remaining stream
 * (Z3660ETH_MAXDEV is 5); the cost of returning was a whole tick of head-of-line
 * blocking for every other bound SAP.
 */
static void
z3660eth_tx_drain(board_index)
int board_index;
{
    z3660eth_t *zp = z3660eth_board[board_index].z3660eth;
    mblk_t *mp;
    int n;

    for (n = 0; n < Z3660ETH_MAXDEV; n++, zp++) {
	if (zp->q) {
	    queue_t *wrq = WR(zp->q);
	    while ((mp = getq(wrq)) != NULL) {
		if (!z3660ethxmit(wrq, mp)) {
		    putbq(wrq, mp);
		    break;		/* this stream only -- try the next */
		}
	    }
	}
    }
}

/*
 * Interrupt service for a future interrupt-driven RX path (not wired into
 * kernel.c by default -- AMIX has no level-6 driver hook; see Q1).  Polls the
 * firmware INT_STATUS, acks ETH, and drains.
 *
 * WARNING: shares the static rxbuff in z3660eth_drain with the poll callout.  If
 * this ISR is ever registered (int2_tbl via board-mod, or an int6_tbl change),
 * DISABLE the poll first -- do not run both drainers concurrently.
 */
void
z3660ethintr()
{
    int i;

    if (panicstr)
	return;

    for (i = 0; i < Z3660ETH_MAXBOARDS; i++) {
	z3660eth_board_t *board = &z3660eth_board[i];
	z3660eth_info_t *info = &board->z3660eth_info;

	if (board->z3660eth_status.board_state == Z3660ETH_BOARD_RUNNING &&
	    info->regs != 0) {
	    if (RDLONG(info->regs, ZZ_INT_STATUS) & ZZ_INT_ETH) {
		WRLONG(info->regs, ZZ_CONFIG, ZZ_CONFIG_ACK_ETH);
		z3660eth_drain(i);
	    }
	}
    }
}

/* ----------------------------------------------------------------------------
 * DLPI protocol engine -- the seven primitives, copied from hydra (Style-1
 * connectionless ethernet).  DL_UNITDATA_REQ feeds z3660ethxmit.
 * ------------------------------------------------------------------------- */
static int
z3660ethproto(q, mp)
queue_t *q;
mblk_t *mp;
{
    register union DL_primitives *p = (union DL_primitives *)mp->b_rptr;
    register z3660eth_t *zp = (z3660eth_t *)q->q_ptr;
    z3660eth_board_t *board = &z3660eth_board[zp->board_index];

    switch (p->dl_primitive) {
    case DL_UNITDATA_REQ:
	if (q->q_first || !z3660ethxmit(q, mp))
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
	board->z3660eth_status.last_bound_sap = zp->sap;
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
	bcopy((caddr_t)board->z3660eth_info.paddress, (caddr_t)mp->b_wptr,
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
	    bcopy((caddr_t)board->z3660eth_info.paddress, (caddr_t)mp->b_wptr,
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
 * (header + payload); z3660eth_drain has already range-checked it to
 * [ETH_HEADER_SIZE, MAX_BUFFER_LENGTH], so `length` below is in [0, 1504].
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
 * advertises IFF_NOTRAILERS, and bring-up is `ifconfig zen0 ... -trailers`), but
 * it was reachable by any crafted frame off the wire.  Trailer-tagged frames now
 * take the ordinary path: sap stays 0x1000..0x1010, no stream binds those SAPs,
 * so they are counted in rx_no_sap and dropped.  Every other ethertype (IP 0x0800,
 * ARP 0x0806, ...) already took the else-arm, so normal RX is byte-for-byte
 * unchanged.  Do not reintroduce a decode arm.
 * ------------------------------------------------------------------------- */
static void
toss_packet_up_stream(packet, board_index, count)
char *packet;
int board_index, count;
{
    int j, length;
    unsigned short sap;
    z3660eth_board_t *board = &z3660eth_board[board_index];
    z3660eth_info_t *info = &board->z3660eth_info;
    z3660eth_t *zp = board->z3660eth;
    static unsigned char broadcast_address[6] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };
    ether_header_t *packet_header = (ether_header_t *)packet;
    int sap_matched = 0;

    sap = packet_header->ether_type;
    board->z3660eth_status.last_rx_sap = sap;
    if (sap == ETHERTYPE_ARP) board->z3660eth_status.rx_arp_seen++;
    if (sap == ETHERTYPE_IP)  board->z3660eth_status.rx_ip_seen++;

    length = count - ETH_HEADER_SIZE;

    for (j = 0; j < Z3660ETH_MAXDEV; j++, zp++) {
	mblk_t *mp = (mblk_t *)NULL;

	if (!zp->q)
	    continue;

	if (zp->flags & Z3660ETH_RAW_DATA) {
	    if (canput(zp->q->q_next)) {
		if ((mp = allocb(count, BPRI_MED)) != NULL) {
		    mp->b_datap->db_type = M_DATA;
		    bcopy((caddr_t)packet, (caddr_t)mp->b_wptr, count);
		    mp->b_wptr += count;
		    putnext(zp->q, mp);
		} else
		    board->z3660eth_status.allocbs_failed++;
	    } else
		board->z3660eth_status.couldnt_put++;
	} else if (zp->sap == sap) {
	    sap_matched = 1;

	    if (bcmp((char *)packet_header->ether_dhost,
		     (char *)info->paddress, ETH_ADDRESS_SIZE) &&
		bcmp((char *)packet_header->ether_dhost,
		     (char *)broadcast_address, sizeof(broadcast_address))) {
		board->z3660eth_status.rx_not_for_us++;
		continue;
	    }

	    if (canput(zp->q->q_next)) {
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
		    board->z3660eth_status.rx_delivered++;
		} else {
		    board->z3660eth_status.allocbs_failed++;
		    if (mp)
			freemsg(mp);
		}
	    } else
		board->z3660eth_status.couldnt_put++;
	}
    }

    if (!sap_matched)
	board->z3660eth_status.rx_no_sap++;
}

/* ----------------------------------------------------------------------------
 * ioctls: SIOC*IFFLAGS ack, board count, config + status snapshots.
 * ------------------------------------------------------------------------- */

/*
 * NAK an M_IOCTL the way the rest of the driver does (M_IOCNAK + errno back up
 * the read side), dropping any data block still attached to it.
 */
static void
z3660eth_ioc_nak(q, mp, err)
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
 * kernel NULL-deref reachable from an unprivileged ioctl() on /dev/zen0.  A
 * b_cont shorter than a pointer would likewise over-read it.
 *
 * Returns 1 with *argp set (0 for the non-TRANSPARENT case, where there is no
 * user address to fetch and the caller does not use it), or 0 => the caller must
 * NAK with EINVAL.
 */
static int
z3660eth_ioc_arg(mp, iocbp, argp)
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
z3660ethioctl(q, mp)
queue_t *q;
mblk_t *mp;
{
    register struct iocblk *iocbp = (struct iocblk *)mp->b_rptr;
    register z3660eth_t *zp = (z3660eth_t *)q->q_ptr;
    register z3660eth_board_t *board = &z3660eth_board[zp->board_index];

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

    case Z3660ETH_NUMBER_OF_BOARDS:
    {
	caddr_t arg;

	if (!z3660eth_ioc_arg(mp, iocbp, &arg)) {
	    z3660eth_ioc_nak(q, mp, EINVAL);
	    return;
	}
	if (mp->b_cont) {
	    freemsg(mp->b_cont);
	    mp->b_cont = NULL;
	}
	if (!(mp->b_cont = allocb(sizeof(int), BPRI_MED))) {
	    z3660eth_ioc_nak(q, mp, ENOMEM);
	    return;
	}
	*(int *)mp->b_cont->b_rptr = z3660eth_number_of_boards;
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

    case Z3660ETH_GET_STATUS:
    {
	caddr_t arg;
	int s;

	if (!z3660eth_ioc_arg(mp, iocbp, &arg)) {
	    z3660eth_ioc_nak(q, mp, EINVAL);
	    return;
	}
	if (mp->b_cont) {
	    freemsg(mp->b_cont);
	    mp->b_cont = NULL;
	}
	if (!(mp->b_cont = allocb(sizeof(z3660eth_status_t), BPRI_MED))) {
	    z3660eth_ioc_nak(q, mp, ENOMEM);
	    return;
	}
	/*
	 * M3: one consistent snapshot.  The poll callout bumps these counters
	 * (RX drain, deferred TX, tx_nolink); unmasked, a tick mid-copy yields a
	 * torn record (packets_received and if_ipackets from different ticks).  108 bytes of
	 * moves; allocb stays outside the window.
	 */
	s = splz3660eth();
	board->z3660eth_status.if_ipackets = board->ifstats.ifs_ipackets;
	board->z3660eth_status.if_opackets = board->ifstats.ifs_opackets;
	board->z3660eth_status.if_ierrors = board->ifstats.ifs_ierrors;
	board->z3660eth_status.if_oerrors = board->ifstats.ifs_oerrors;
	*(z3660eth_status_t *)mp->b_cont->b_rptr = board->z3660eth_status;
	splx(s);
	mp->b_cont->b_wptr += sizeof(z3660eth_status_t);
	if (iocbp->ioc_count == TRANSPARENT) {
	    struct copyreq *creq = (struct copyreq *)mp->b_rptr;
	    mp->b_datap->db_type = M_COPYOUT;
	    creq->cq_addr = arg;
	    mp->b_wptr = mp->b_rptr + sizeof *creq;
	    creq->cq_size = sizeof(z3660eth_status_t);
	    creq->cq_flag = 0;
	    creq->cq_private = (mblk_t *)0;
	} else {
	    iocbp->ioc_count = sizeof(z3660eth_status_t);
	    mp->b_datap->db_type = M_IOCACK;
	}
	putnext(RD(q), mp);
	return;
    }

    /*
     * Declared in z3660ethuser.h but never implemented.  Given their own case
     * labels rather than left to fall into the default arm so the state is
     * visible at the point a future implementor will look: these are recognised
     * commands with no implementation, not unknown commands.  Behaviour is the
     * same EINVAL NAK either way.
     */
    case Z3660ETH_CLEAR_STATUS:
    case Z3660ETH_GET_CONFIG:
	z3660eth_ioc_nak(q, mp, EINVAL);
	return;

    default:
	z3660eth_ioc_nak(q, mp, EINVAL);
	return;
    }
}

/*
 * Boot init.  NOT registered in init_tbl (lazy-open model, like aen) so the
 * GEM-less WinUAE build box boots cleanly and open() simply ENXIOs.  Provided
 * for completeness / future use.
 */
void
z3660ethinit()
{
    z3660eth_autoconfig();
}
