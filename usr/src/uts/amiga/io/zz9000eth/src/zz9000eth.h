#ifndef _AMIGA_ZZ9000ETH_H
#define _AMIGA_ZZ9000ETH_H
/*
 * zz9000eth.h -- kernel-side definitions for the MNT ZZ9000 AMIX ethernet
 * driver, derived from Jussi Alanärä's z3660eth (io/z3660net): the same
 * STREAMS/DLPI driver, the ZZ9000's firmware mailbox instead of the Z3660's.
 *
 * The interface below is stated from MNT's ZZ9000 firmware and AmigaOS driver
 * (facts only; no code is taken from them, they are GPL-3.0-or-later):
 * ZZ9000OS src/zz_regs.h (register numbers), main.c (the Zorro register and
 * window decode, REG_ZZ_CONFIG, the backlog nag), ethernet.c (the RX backlog,
 * FRAME_MAX_BACKLOG 32, the [len][serial] slot header, ethernet_send_frame's
 * results 0..4), mntzorro.v (16-bit Zorro II register reads: A1=0 -> bits
 * 31..16, A1=1 -> bits 15..0; the RX window at 0x2000 reads the slot the
 * firmware selects), and ZZ9000Net.device device.c (FindConfigDev 0x6d6e
 * product 4, then 3; INT6 server; RX at +0x2000, TX at +0x8000).  See
 * ../README.md, "The ZZ9000 interface".
 *
 * What the Z3660 firmware (which descends from this one) changed, and this
 * driver therefore does differently from z3660eth:
 *   - 16-bit registers in the ZZ9000's own register page (0x04, 0x80..0x88),
 *     not 32-bit ones at 0x104..0x1A8;
 *   - one RX window at board+0x2000 that always shows the firmware's current
 *     slot (no RX_ADDR cursor; the other slots cannot be read), and the TX
 *     frame at board+0x8000 -- both inside the 64 KB register area, not in
 *     DDR windows at +0x07ED0000;
 *   - no catch-up fake of the slot-0 serial: after the driver has caught up,
 *     slot 0 shows the first frame of that round again, with an OLDER serial,
 *     so a frame counts as new only when its serial is ahead of the last one
 *     (zz9000eth_drain);
 *   - the ETH_TX result reads back byte-swapped (result << 8 for 0..4);
 *   - the default MAC is the firmware's 68:82:F2:00:01:00 unless AmigaOS (or
 *     anyone) wrote another one before AMIX booted;
 *   - no ZS-05 address-capture defect: plain accesses, no arming reads.
 */

#include "sys/immu.h"		/* NBPP: the page size the window page counts round up to */
#include "zz9000ethuser.h"

/* ---- board identity: AutoConfig only (the zz9000 graphics driver's board) ---- */
#define ZZ9000ETH_PROD_Z2	0x6d6e0003	/* MNT ZZ9000, Zorro II (4 MB) */
#define ZZ9000ETH_PROD_Z3	0x6d6e0004	/* MNT ZZ9000, Zorro III */

/* ---- ZZ9000 registers (board_base-relative, 16-bit big-endian MMIO) ----
 * The graphics driver (io/zz9000) owns the rest of the register page and the
 * framebuffer at +0x10000; it touches none of these, and this driver touches
 * nothing else.  REG_ZZ_CONFIG's enable branch sets the ethernet interrupt
 * only (the ZZ9000AX audio interrupt has its own enable register). */
#define ZZ_CONFIG	0x04	/* W: 1 = ETH interrupt on, 0 = off, 24 = ack ETH;
				   R: pending Amiga interrupt bits (ETH=1, AUDIO=2) */
#define ZZ_ETH_TX	0x80	/* W byte length = send the TX frame (synchronous,
				   up to ~1 ms); R = result << 8 (0 ok, 1 no link,
				   2/3 descriptor error, 4 timeout) */
#define ZZ_ETH_RX	0x82	/* W (any) = the current RX slot is consumed */
#define ZZ_ETH_MAC_HI	0x84	/* R/W MAC[0..1] = (b0<<8)|b1 */
#define ZZ_ETH_MAC_HI2	0x86	/* R/W MAC[2..3] */
#define ZZ_ETH_MAC_LO	0x88	/* R/W MAC[4..5]; a write sets the GEM's address */
#define ZZ_FW_VERSION	0xC0	/* R (major << 8) | minor */

#define ZZ_CONFIG_ENABLE	1	/* enable eth interrupt delivery */
#define ZZ_CONFIG_DISABLE	0
#define ZZ_CONFIG_ACK_ETH	(8|16)	/* = 24: bit3 gated ack/clear of ETH */
#define ZZ_INT_ETH		1	/* ZZ_CONFIG read: ethernet RX pending */

/* ---- frame windows (board_base-relative) ----
 *   0x2000..0x5FFF  RX: the firmware's current backlog slot, [0..1] length,
 *                   [2..3] serial, frame from +4 (read only)
 *   0x6000..0x7FFF  the board's AutoBoot ROM (never touched)
 *   0x8000..0x9FFF  TX frame (write the frame, then its length to ZZ_ETH_TX)
 * One mapping covers 0x2000..0x9FFF: frame = board+0x2000, the RX slot is
 * frame+0, txwin = frame+0x6000.  Bytes are primary, page counts derived (the
 * page counts only size the sptalloc() arm). */
#define ZZ_FRAME_OFF	 0x2000		/* the RX window, start of the mapping */
#define ZZ_TXWIN_OFF	 0x6000		/* TX window, from ZZ_FRAME_OFF (= +0x8000) */
#define ZZ_FRAME_BYTES	 0x8000		/* 0x2000..0x9FFF */
#define ZZ_FRAME_SIZE	 2048		/* firmware slot stride: the largest slot */
#define ZZ_RX_FRAME_PAD	 4		/* per-slot header: [0..1]=len [2..3]=serial */
#define ZZ_RX_RING_SLOTS 32		/* firmware FRAME_MAX_BACKLOG (tail drop) */

#define ZZ_REGS_BYTES	(ZZ_FW_VERSION + 4)	/* the register page up to FW_VERSION */
#define ZZ_REGS_PAGES	((ZZ_REGS_BYTES + NBPP - 1) / NBPP)
#define ZZ_FRAME_PAGES	((ZZ_FRAME_BYTES + NBPP - 1) / NBPP)
/* highest board-relative byte the driver ever touches (+1) */
#define ZZ_WINDOW_TOP	(ZZ_FRAME_OFF + ZZ_FRAME_BYTES)	/* 0xA000 */

#ifndef NBPP
#error "zz9000eth.h: NBPP undefined -- <sys/immu.h> must be included first"
#endif
#if (ZZ_FRAME_PAGES * NBPP) < ZZ_FRAME_BYTES
#error "ZZ_FRAME_PAGES does not cover ZZ_FRAME_BYTES -- page round-up is broken"
#endif
#if ZZ_TXWIN_OFF + ZZ_FRAME_SIZE > ZZ_FRAME_BYTES
#error "the TX window must lie inside the frame mapping"
#endif

/* board register access: 16-bit, nothing else */
#define ZZ_RD16(r_, o_)		(*(volatile unsigned short *)((r_) + (o_)))
#define ZZ_WR16(r_, o_, v_)	(*(volatile unsigned short *)((r_) + (o_)) = (unsigned short)(v_))

/* ---- ethernet framing (mirrors aen.h / hydra.h) ---- */
#define ETH_ADDRESS_SIZE	6
#define ETH_TYPE_SIZE		2
#define ETH_LENGTH_SIZE		2
#define ETH_CRC_LEN		4
#define ETH_HEADER_SIZE		(ETH_ADDRESS_SIZE + ETH_ADDRESS_SIZE + ETH_TYPE_SIZE)
#define ETH_MAXDATA		1500
#define ETH_MAXPACKET		(ETH_HEADER_SIZE + ETH_MAXDATA)
/*
 * Min frame: the firmware delivers the raw XEmacPs_BdGetLength (as the Z3660
 * firmware, which inherited it) -- so TX is padded to 60 (ETH_MINPACKET), and
 * RX length is NOT reduced by ETH_CRC_LEN (do NOT inherit aen's -4); whether a
 * trailing FCS comes along is to be seen on a real board (z3660net's
 * ETHERNET-SCOPING.md Q4).
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
 * above toss_packet_up_stream() in zz9000eth.c.  Do not reintroduce a decode arm.
 */
#define ETHERTYPE_TRAIL		0x1000
#define ETHERTYPE_NTRAILER	16

#define ZZ9000ETH_MTU		ETH_MAXDATA

typedef u_char ether_addr_t[6];

struct ether_header
{
    ether_addr_t	ether_dhost;
    ether_addr_t	ether_shost;
    u_short		ether_type;
};
typedef struct ether_header ether_header_t;

/* ---- driver limits + STREAMS water marks ---- */
#define ZZ9000ETH_MAXDEV		5	/* SAP streams per board */
#define ZZ9000ETH_MAXBOARDS	1	/* one ZZ9000 */
#define ZZ9000ETH_MIN		ETH_MINPACKET
#define ZZ9000ETH_MAX		ETH_MAXDATA
#define ZZ9000ETH_HIWAT		(1024 * 5)
#define ZZ9000ETH_LOWAT		(2 * ZZ9000ETH_HIWAT / 3)
#define ZZ9000ETH_INIT_PRI	(PZERO - 1)

/*
 * Interrupt masking.  spl6()/splx() are the REAL stock primitives: static inline
 * in <sys/inline.h> (inline.h:112-260).  An earlier revision of this header said
 * AMIX has none and no-opped them; that was wrong -- the UNDEF seen in the first
 * kernel link came from not including inline.h, and until gcc-cross-amix 7a1d727
 * (-Dvolatile=__volatile__) the -traditional cross build would have deleted the
 * asm anyway (a code review).  On this kernel spl5/spl6/spl7/spltty/splhi all map
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
#define splzz9000eth()		spl6()

#define ZZ9000ETH_POLL_TICKS	1	/* RX poll period (clock ticks) */

/*
 * RX backpressure (zz9000eth_drain).  A received frame is never freed because
 * an upstream queue is full: it stays in the firmware backlog and the drain
 * stops there, to resume from the read service procedure (back-enabled when
 * the upstream queue drains) or the next poll tick.  One guard bounds the hold,
 * and past it a frame is dropped for a stream that cannot take it
 * (couldnt_put):
 *   HOLD_TICKS  a stream that takes nothing for this long is stuck, not slow.
 * (z3660eth also bounds the hold by the backlog depth, reading a slot further
 * on; the ZZ9000's RX window shows only the current slot, and a full backlog
 * of 32 only tail-drops new frames, so the time bound is the only one.)
 * RX_PEEK is the part of a frame copied before the verdict (the ethernet header,
 * rounded up so the payload copy keeps its alignment).
 */
#define ZZ9000ETH_RX_HOLD_TICKS	(HZ / 2)
#define ZZ9000ETH_RX_PEEK	16

#define blklen(bp)		((bp)->b_wptr - (bp)->b_rptr)

/* ---- per-stream state (one per opened minor/SAP) ---- */
typedef struct
{
    queue_t		*q;
    int			state;		/* DL_UNATTACHED / DL_UNBOUND / DL_IDLE */
    int			board_index;
    int			flags;		/* ZZ9000ETH_RAW_DATA */
    unsigned short	sap;		/* bound ethertype */
} zz9000eth_t;

/* ---- per-board hardware state ---- */
typedef struct
{
    long		 board_base;	/* physical board base (AutoConfig) */
    int			 product;	/* 3 = Zorro II, 4 = Zorro III */
    volatile u_char	*regs;		/* mapped board_base+0 (register page) */
    volatile u_char	*frame;		/* mapped board_base+ZZ_FRAME_OFF (RX slot) */
    volatile u_char	*txwin;		/* frame + ZZ_TXWIN_OFF */
    unsigned char	 paddress[6];	/* station MAC */
    unsigned short	 last_serial;	/* serial of the last slot consumed */
    unsigned short	 rx_hold_serial; /* serial of the frame left in the ring */
    unsigned char	 rx_held;	/* a frame is held at the ring head */
    long		 rx_hold_since;	/* lbolt when rx_hold_serial was first held */
    volatile int	 tx_busy;	/* serialises the single txwin */
    int			 poll_id;	/* timeout() id for the RX poll callout */
    int			 poll_running;
    int			 map_verdict;	/* 0 = not probed/mapped; else the cached
					   errno of a failed probe (ENXIO) */
    unsigned char	 spt_mapped;	/* regs/frame came from sptalloc()/dev_kvmap */
    unsigned char	 nomem_said;	/* ENOMEM console line printed once */
} zz9000eth_info_t;

typedef struct
{
    zz9000eth_t		zz9000eth[ZZ9000ETH_MAXDEV];
    zz9000eth_status_t	zz9000eth_status;
    zz9000eth_info_t	zz9000eth_info;
    struct ifstats	ifstats;
    int			if_flags;
} zz9000eth_board_t;

#endif /* _AMIGA_ZZ9000ETH_H */
