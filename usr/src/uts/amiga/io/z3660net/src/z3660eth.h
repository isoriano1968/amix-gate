#ifndef _AMIGA_Z3660ETH_H
#define _AMIGA_Z3660ETH_H
/*
 * z3660eth.h -- kernel-side definitions for the Z3660 native AMIX ethernet
 * driver (the STREAMS/DLPI analogue of the z3660.c SCSI driver).  Includes the
 * Z3660 ethernet register map + frame-window layout, all VERIFIED against the
 * firmware/guest source (z3660-drivers/common/z3660_regs.h, Z3660/src/
 * ethernet.c + memorymap.h + rtg/rtg.c).  See ETHERNET-SCOPING.md.
 */

#include "sys/immu.h"		/* NBPP: the page size the window page counts round up to */
#include "z3660ethuser.h"

/* ---- board identity / discovery (same combo board as the z3660.c SCSI) ---- */
#define Z3660_PROD	0x144B0001	/* autocon pc = (manuf<<16)|product */
#define Z3660_FIXED	0x10000000	/* combo window base when not autoconfigured */
#define VPOSR		0xDFF004	/* Agnus/Alice id: bits 8-14 >= 0x22 => AGA */

/* ---- Z3660 ethernet registers (board_base-relative, 32-bit big-endian MMIO).
 * These live in the RTG register page at board_base+0x000 (piscsi is +0x2000). */
#define ZZ_CONFIG	0x104	/* W: enable=1, ack ETH=24 (8|16), disable=0 */
#define ZZ_ETH_TX	0x190	/* W byte-length = trigger TX; R = TX result */
#define ZZ_ETH_RX	0x194	/* W (any value) = advance/free current RX slot */
#define ZZ_ETH_MAC_HI	0x198	/* R/W: MAC[0..1] = (b0<<8)|b1 */
#define ZZ_ETH_MAC_LO	0x19C	/* R/W: MAC[2..5]; write triggers GEM SetMac */
#define ZZ_ETH_RX_ADDR	0x1A4	/* R: board-relative byte offset of cur RX slot */
#define ZZ_INT_STATUS	0x1A8	/* R: pending bits ETH=1, AUDIO=2, USB=4 */

/* REG_ZZ_CONFIG transactions (mutually exclusive firmware branches, rtg.c:1150) */
#define ZZ_CONFIG_ENABLE	1	/* enable eth interrupt delivery */
#define ZZ_CONFIG_DISABLE	0
#define ZZ_CONFIG_ACK_ETH	(8|16)	/* = 24: bit3 gated ack/clear of ETH */

#define ZZ_INT_ETH	1	/* REG_ZZ_INT_STATUS: ethernet RX pending */

/* ---- frame windows (board_base-relative; real DDR through the Zorro III win).
 * memorymap.h: RX_BACKLOG=0x07ED0000 ("32*2048 = 64kB"), TX_FRAME=0x07EE0000,
 * GEM RX_FRAME=0x07EF0000.  FRAME_MAX_BACKLOG(64) overruns the 32-slot ring --
 * latent firmware bug; mitigated by eager draining + mapping the full range. */
#define ZZ_RXBACKLOG_OFF 0x07ED0000	/* start of the RX backlog ring */
#define ZZ_TXFRAME_OFF	 0x07EE0000	/* TX staging frame (one frame) */
#define ZZ_FRAME_SIZE	 2048		/* per-slot stride */
#define ZZ_RX_FRAME_PAD	 4		/* per-slot header: [0..1]=len [2..3]=serial */
#define ZZ_RX_RING_SLOTS 32		/* real usable slots before TX_FRAME */
#define ZZ_RX_MAX_SLOTS	 64		/* firmware FRAME_MAX_BACKLOG (overrun range) */

/*
 * Window extents.  BYTES ARE PRIMARY HERE; PAGE COUNTS ARE DERIVED.
 *
 * ZZ_FRAME_SIZE (2048) above is the firmware's per-slot PROTOCOL STRIDE.  It is
 * not a page size -- it only happens to equal the kernel page size on the kernel
 * this driver ships against (immu.h: NBPP 2048).  The board window geometry is a
 * property of the firmware, so it is stated in bytes and stays fixed no matter
 * what NBPP is; the page counts, which exist only to size the sptalloc() calls
 * on the non-direct-map path, are rounded up from those bytes for whatever NBPP
 * the kernel is built with.
 *
 * Getting that dependency backwards is a real hazard, not a style point.  Take
 * the window span from a page COUNT -- "64 pages" -- and on a 4096-byte-page
 * kernel the span becomes 256 KB; make the count follow the page size instead
 * ("128 KB worth of pages" spelled as a halved count) and the span silently
 * becomes 64 KB, which drops ZZ_WINDOW_TOP from 0x07EF0000 to exactly
 * ZZ_TXFRAME_OFF.  ZZ_WINDOW_TOP is the direct-map admission bound in
 * z3660eth_map(): at that value the guard would admit a board whose TX staging
 * frame lies past the identity-mapped section, i.e. it would stop covering the
 * very window it exists to check.  Hence ZZ_FRAME_SPAN/ZZ_WINDOW_TOP come from
 * ZZ_FRAME_BYTES directly and never from a page count, and the assertions below
 * fail the build if that is ever reversed.
 *
 *   regs  : ZZ_REGS_BYTES at board_base+0 -- the register file 0x104..0x1A8,
 *           read as 32-bit longs, so 0x1AC bytes; rounds up to one page on any
 *           sane NBPP (it was a hardcoded 1 page, correct only for NBPP >= 0x1AC).
 *   frame : ZZ_FRAME_BYTES at board_base+0x07ED0000 -- the full 64-slot overrun
 *           range (0x20000 = ZZ_RX_MAX_SLOTS * ZZ_FRAME_SIZE) so that every
 *           ZZ_ETH_RX_ADDR the firmware can produce lands inside the mapping;
 *           txwin = frame_region + (0x07EE0000-0x07ED0000) = +0x10000.
 *
 * The direct-map path uses the byte spans; only sptalloc() uses the page counts.
 * See the mapping-strategy comment above z3660eth_map() in z3660eth.c.
 */
#define ZZ_REGS_BYTES	(ZZ_INT_STATUS + 4)	/* highest register + one long */
#define ZZ_FRAME_BYTES	0x20000			/* firmware frame window, in bytes */
#define ZZ_REGS_PAGES	((ZZ_REGS_BYTES + NBPP - 1) / NBPP)	/* 1 @ NBPP 2048 */
#define ZZ_FRAME_PAGES	((ZZ_FRAME_BYTES + NBPP - 1) / NBPP)	/* 64 @ NBPP 2048 */
#define ZZ_FRAME_SPAN	ZZ_FRAME_BYTES		/* NEVER from a page count */
#define ZZ_TXWIN_OFF	(ZZ_TXFRAME_OFF - ZZ_RXBACKLOG_OFF)	/* 0x10000 */
/* highest board-relative byte the driver ever touches (+1) */
#define ZZ_WINDOW_TOP	(ZZ_RXBACKLOG_OFF + ZZ_FRAME_BYTES)	/* 0x07EF0000 */

/*
 * Compile-time guards on the above.  These are plain preprocessor conditionals:
 * when an assertion holds, its #error sits in a SKIPPED group, and a skipped
 * group is scanned only for conditional-directive nesting -- so a preprocessor
 * that does not implement #error at all still compiles this header unharmed.
 */
#ifndef NBPP
#error "z3660eth.h: NBPP undefined -- <sys/immu.h> must be included first"
#endif
#if (ZZ_FRAME_PAGES * NBPP) < ZZ_FRAME_BYTES
#error "ZZ_FRAME_PAGES does not cover ZZ_FRAME_BYTES -- page round-up is broken"
#endif
#if (ZZ_REGS_PAGES * NBPP) < ZZ_REGS_BYTES
#error "ZZ_REGS_PAGES does not cover ZZ_REGS_BYTES -- page round-up is broken"
#endif
#if ZZ_WINDOW_TOP != (ZZ_RXBACKLOG_OFF + ZZ_FRAME_BYTES)
#error "ZZ_WINDOW_TOP must be ZZ_RXBACKLOG_OFF + ZZ_FRAME_BYTES, never a page count"
#endif
#if ZZ_FRAME_BYTES < (ZZ_RX_MAX_SLOTS * ZZ_FRAME_SIZE)
#error "ZZ_FRAME_BYTES must cover the whole ZZ_RX_MAX_SLOTS overrun range"
#endif

/* ---- ethernet framing (mirrors aen.h / hydra.h) ---- */
#define ETH_ADDRESS_SIZE	6
#define ETH_TYPE_SIZE		2
#define ETH_LENGTH_SIZE		2
#define ETH_CRC_LEN		4
#define ETH_HEADER_SIZE		(ETH_ADDRESS_SIZE + ETH_ADDRESS_SIZE + ETH_TYPE_SIZE)
#define ETH_MAXDATA		1500
#define ETH_MAXPACKET		(ETH_HEADER_SIZE + ETH_MAXDATA)
/*
 * Min frame: the Z3660 firmware delivers the raw XEmacPs_BdGetLength and never
 * strips the FCS (no XEmacPs_SetOptions FCS_STRIP) -- so the on-wire minimum the
 * driver should pad TX to is 60 (ETH_MINPACKET), and RX length is NOT reduced by
 * ETH_CRC_LEN (do NOT inherit aen's -4).  See ETHERNET-SCOPING.md Q4.
 */
#define ETH_MINDATA		46		/* 60-byte min frame - 14 hdr */
#define ETH_MINPACKET		(ETH_HEADER_SIZE + ETH_MINDATA)	/* 60 */
#define MAX_BUFFER_LENGTH	1518

#define ETHERTYPE_IP		0x0800
#define ETHERTYPE_ARP		0x0806
/*
 * 4.2BSD trailer encapsulation.  Retained ONLY to document the ethertype range
 * 0x1000..0x1010 that this driver deliberately does NOT decode: the inherited
 * aen/hydra trailer arm read up to ~6.7 KB past the 1518-byte RX buffer and could
 * undersize the payload mblk (negative trailn) -- see the no-trailer-decode note
 * above toss_packet_up_stream() in z3660eth.c.  Do not reintroduce a decode arm.
 */
#define ETHERTYPE_TRAIL		0x1000
#define ETHERTYPE_NTRAILER	16

#define Z3660ETH_MTU		ETH_MAXDATA

typedef u_char ether_addr_t[6];

struct ether_header
{
    ether_addr_t	ether_dhost;
    ether_addr_t	ether_shost;
    u_short		ether_type;
};
typedef struct ether_header ether_header_t;

/* ---- driver limits + STREAMS water marks ---- */
#define Z3660ETH_MAXDEV		5	/* SAP streams per board */
#define Z3660ETH_MAXBOARDS	1	/* one Z3660 */
#define Z3660ETH_MIN		ETH_MINPACKET
#define Z3660ETH_MAX		ETH_MAXDATA
#define Z3660ETH_HIWAT		(1024 * 5)
#define Z3660ETH_LOWAT		(2 * Z3660ETH_HIWAT / 3)
#define Z3660ETH_INIT_PRI	(PZERO - 1)

/*
 * Interrupt masking.  spl6()/splx() are the REAL stock primitives: static inline
 * in <sys/inline.h> (inline.h:112-260).  An earlier revision of this header said
 * AMIX has none and no-opped them; that was wrong -- the UNDEF seen in the first
 * kernel link came from not including inline.h, and until gcc-cross-amix 7a1d727
 * (-Dvolatile=__volatile__) the -traditional cross build would have deleted the
 * asm anyway (review T-9).  On this kernel spl5/spl6/spl7/spltty/splhi all map
 * to _spl4, i.e. SR = 0x2400 (inline.h:239-251): IPL 4, which blocks the level-2
 * CIA-A clock and therefore every callout, including the RX poll.
 *
 * _spl4 SETS the IPL, it does not raise it: called above IPL 4 it would LOWER
 * it.  Every caller here runs at base (IPL 0) or in the poll callout (which
 * dispatches at IPL 4), so it only ever raises or re-states.  Which sections are
 * masked, and the race each pair closes, is documented in NOTES.md
 * ("Interrupt masking").  Keep every masked window a handful of instructions:
 * never across the ~1 ms synchronous TX trigger, allocb(), or a STREAMS put.
 */
#include "sys/inline.h"
#define splz3660eth()		spl6()

#define Z3660ETH_POLL_TICKS	1	/* RX poll period (clock ticks) */

#define blklen(bp)		((bp)->b_wptr - (bp)->b_rptr)
#ifndef DELAY
#define DELAY(x)		{ int _d = (x) * 100; while (_d--) ; }
#endif

/* ---- per-stream state (one per opened minor/SAP) ---- */
typedef struct
{
    queue_t		*q;
    int			state;		/* DL_UNATTACHED / DL_UNBOUND / DL_IDLE */
    int			board_index;
    int			flags;		/* Z3660ETH_RAW_DATA */
    unsigned short	sap;		/* bound ethertype */
} z3660eth_t;

/* ---- per-board hardware state ---- */
typedef struct
{
    long		 board_base;	/* physical board base (0x10000000 etc.) */
    volatile u_char	*regs;		/* mapped board_base+0 (register page) */
    volatile u_char	*frame;		/* mapped board_base+0x07ED0000 region */
    volatile u_char	*txwin;		/* frame + ZZ_TXWIN_OFF */
    unsigned char	 paddress[6];	/* station MAC */
    unsigned short	 last_serial;	/* RX ring drain cursor (firmware serial) */
    volatile int	 tx_busy;	/* serialises the single txwin */
    int			 poll_id;	/* timeout() id for the RX poll callout */
    int			 poll_running;
    int			 map_verdict;	/* 0 = not probed/mapped; else the cached
					   errno of a failed probe (ENXIO) */
    unsigned char	 spt_mapped;	/* regs/frame came from sptalloc() */
    unsigned char	 nomem_said;	/* ENOMEM console line printed once */
} z3660eth_info_t;

typedef struct
{
    z3660eth_t		z3660eth[Z3660ETH_MAXDEV];
    z3660eth_status_t	z3660eth_status;
    z3660eth_info_t	z3660eth_info;
    struct ifstats	ifstats;
    int			if_flags;
} z3660eth_board_t;

#endif /* _AMIGA_Z3660ETH_H */
