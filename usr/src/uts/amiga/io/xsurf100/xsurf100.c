/*
** xsurf100.c -- Individual Computers X-Surf 100 (ASIX AX88796B) for AMIX:
** the X-Surf backend of the shared DP8390 core (../dp8390/dp8390.c), as
** the STREAMS DLPI driver xsurf100info, /dev/xsf0, interface xsf0.
**
** The board, from Linux's xsurf100.c/ax88796.c and iComp's register notes
** (facts only, no code taken):
**   AutoConfig       manufacturer 4626 (0x1212), product 100 (0x64);
**                    Zorro II or, in a Zorro III slot, Zorro III
**   board + 0x0040   interrupt status, 16 bits: & 0xaaaa = network IRQ
**   board + 0x0800   AX88796B register n at + n*4 (0x00-0x0f: DP8390;
**                    0x10 data port, 0x17 GPOC, 0x1f reset)
**   board + 0x8880   remote DMA read FIFO, 0x80 bytes: each 32-bit access
**                    is two 16-bit data port cycles
**   board + 0x8c80   remote DMA write FIFO, the same
**   buffer RAM       16 KB, pages 0x40-0x7f, reached by remote DMA only
**   DCR              0x48 (byte wide) to read the PROM, 0x49 (word wide)
**                    in operation; RCR base 0x40
**   GPOC bit 6       PHY power down (cleared at open, set at close)
**   MAC              the PROM at buffer address 0, 32 bytes read byte wide,
**                    each byte doubled; the internal PHY sets the MAC's
**                    duplex itself (Linux does nothing to it either)
**
** Mapping: the A3000 has only Zorro III slots, so the card configures at
** 0x40000000 or above, outside the kernel's identity-mapped low 1 GB.
** Stock 68030 kernels: sptalloc() of the 0x9000-byte window (the 030 line
** has no cache-inhibit PTE bit; the window is cache-inhibited by the
** board's CINH on the Zorro III bus).  68040/68060 port kernels
** (-DXSURF100_KVMAP): dev_kvmap() noncacheable serialised, under the name
** xsurf100_kvmap (the kit carries its own copy).  A board in the low 1 GB
** (Zorro II, or Zorro III below VSECT1) is used through the identity map.
**
** Silent without a board: only AutoConfig is asked, at boot and at open.
** Global symbols: xsurf100info, xsurf100intr (level 2), xsurf100init.
*/

#include "sys/types.h"
#include "sys/param.h"
#include "sys/signal.h"
#include "sys/errno.h"
#include "sys/psw.h"
#include "sys/pcb.h"
#include "sys/user.h"
#include "sys/sysmacros.h"
#include "sys/immu.h"
#include "sys/stream.h"
#include "sys/stropts.h"

#include "sys/queue.h"
#include "sys/cio_defs.h"
#include "sys/sbd.h"
#include "sys/debug.h"
#include "sys/inline.h"
#include "sys/systm.h"
#include "sys/cred.h"
#include "sys/dlpi.h"
#include "sys/ddi.h"
#include "sys/cmn_err.h"
#include "sys/socket.h"
#include "sys/sockio.h"
#include "net/if.h"
#include "xsurf100user.h"

#define DP_NAME		"xsf"
#define DP_MODID	0x7873
#define DP_IOC		XSURF100IOC
#define DP_MAXBOARDS	2
#define DP_BOARD_ID	0x12120064	/* manufacturer 0x1212 (4626), product 0x64 */
#define DP_RAM_START	0x40		/* 16 KB buffer RAM: pages 0x40..0x7f */
#define DP_TX_START	0x40
#define DP_RX_START	(0x40 + DP_TX_PAGES)
#define DP_STOP		0x80
#define DP_DCR		(NE_DCR_FT1 | NE_DCR_LS | NE_DCR_WTS)	/* 0x49 */
#define DP_RCR		(0x40 | NE_RCR_AB | NE_RCR_AM)

typedef xsurf100_status_t	dp_status_t;
typedef xsurf100_test_t		dp_test_t;
typedef xsurf100_config_t	dp_config_t;

#include "dp8390.h"

#define XS_IRQ_STATUS	0x0040
#define XS_NIC		0x0800
#define XS_STRIDE	4
#define XS_DATAPORT	0x10
#define XS_GPOC		0x17
#define XS_GPOC_PPD	0x40
#define XS_RESET	0x1f
#define XS_FIFO_READ	0x8880
#define XS_FIFO_WRITE	0x8c80
#define XS_FIFO_SIZE	0x80
#define XS_WINDOW	0x9000
#define XS_DCR_PROM	(NE_DCR_FT1 | NE_DCR_LS)		/* 0x48 */

#define XS_MAP_DIRECT	0
#define XS_MAP_KERNEL	1

#ifdef XSURF100_KVMAP
extern char *xsurf100_kvmap();
#else
extern caddr_t sptalloc();
#endif

static unsigned char
dp_inb(board, reg)
dp_board_t *board;
int reg;
{
    return *(board->info.nic_base + reg * XS_STRIDE);
}

static void
dp_outb(board, reg, val)
dp_board_t *board;
int reg, val;
{
    *(board->info.nic_base + reg * XS_STRIDE) = val;
}

#define XS_DATA(board) \
    ((volatile unsigned short *)((board)->info.nic_base + XS_DATAPORT * XS_STRIDE))

/* a remote DMA of count bytes (even) at offset in the given direction */
static void
xs_dma_start(board, count, offset, cmd)
dp_board_t *board;
int count, offset, cmd;
{
    dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STA | NE_CR_NODMA);
    dp_outb(board, NE_ISR, NE_ISR_RDC);
    dp_outb(board, NE_RBCNT0, count & 0xff);
    dp_outb(board, NE_RBCNT1, (count >> 8) & 0xff);
    dp_outb(board, NE_RSAR0, offset & 0xff);
    dp_outb(board, NE_RSAR1, (offset >> 8) & 0xff);
    dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STA | cmd);
}

static int
xs_range_ok(len, offset)
int len, offset;
{
    return len >= 0 && offset >= (DP_RAM_START << 8) &&
	   offset + ((len + 1) & ~1) <= (DP_STOP << 8);
}

static int
dp_be_ram_read(board, buf, len, offset)
dp_board_t *board;
unsigned char *buf;
int len, offset;
{
    volatile unsigned long *src;
    unsigned short w;
    int i, n;

    if (!xs_range_ok(len, offset))
	return 0;
    if (len == 0)
	return 1;
    xs_dma_start(board, (len + 1) & ~1, offset, NE_CR_RDMA_PULL);

    n = len & ~3;
    src = (volatile unsigned long *)(board->info.board_base + XS_FIFO_READ);
    for (i = 0; i < n; i += 4)
    {
	if ((i & (XS_FIFO_SIZE - 1)) == 0)
	    src = (volatile unsigned long *)(board->info.board_base + XS_FIFO_READ);
	*(unsigned long *)(buf + i) = *src++;
    }
    if (len - n >= 2)
    {
	*(unsigned short *)(buf + n) = *XS_DATA(board);
	n += 2;
    }
    if (len - n == 1)
    {
	w = *XS_DATA(board);
	buf[n] = w >> 8;		/* the bus is big-endian: first byte high */
    }
    dp_outb(board, NE_ISR, NE_ISR_RDC);
    return 1;
}

static int
dp_be_ram_write(board, buf, len, offset)
dp_board_t *board;
unsigned char *buf;
int len, offset;
{
    volatile unsigned long *dst;
    int i, n;

    if (!xs_range_ok(len, offset))
	return 0;
    if (len == 0)
	return 1;
    xs_dma_start(board, (len + 1) & ~1, offset, NE_CR_RDMA_STORE);

    n = len & ~3;
    dst = (volatile unsigned long *)(board->info.board_base + XS_FIFO_WRITE);
    for (i = 0; i < n; i += 4)
    {
	if ((i & (XS_FIFO_SIZE - 1)) == 0)
	    dst = (volatile unsigned long *)(board->info.board_base + XS_FIFO_WRITE);
	*dst++ = *(unsigned long *)(buf + i);
    }
    if (len - n >= 2)
    {
	*XS_DATA(board) = (buf[n] << 8) | buf[n + 1];
	n += 2;
    }
    if (len - n == 1)
	*XS_DATA(board) = buf[n] << 8;	/* the pad byte is 0 */

    /* the NIC has the data when the remote byte count runs out (RDC) */
    for (i = 0; i < 20000; i++)
	if (dp_inb(board, NE_ISR) & NE_ISR_RDC)
	    break;
    if (i == 20000)
    {
	dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STA | NE_CR_RDMA_ABORT);
	return 0;
    }
    dp_outb(board, NE_ISR, NE_ISR_RDC);
    return 1;
}

/* AutoConfig: Zorro II (64 KB aligned) or Zorro III, and room for the window */
static int
dp_be_addr_ok(addr, size)
long addr, size;
{
    unsigned long a = (unsigned long)addr;

    if (size != 0 && (unsigned long)size < XS_WINDOW)
	return 0;
    if ((a & 0xFFFF) != 0)
	return 0;
    return (a >= 0x00E80000UL && a < 0x00F00000UL) ||
	   (a >= 0x00200000UL && a < 0x00A00000UL) ||
	   (a >= 0x10000000UL && a <= 0xFF000000UL - XS_WINDOW);
}

static int
dp_be_map(board)
dp_board_t *board;
{
    dp_info_t *info = &board->info;
    unsigned long phys = info->board_phys;
    volatile unsigned char *va;

    if (info->board_base)
	return 0;			/* mapped at an earlier open: kept */
    if (phys < (unsigned long)VSECT1 && (unsigned long)VSECT1 - phys >= XS_WINDOW)
    {
	va = (volatile unsigned char *)phystokv(phys);
	info->mapped = XS_MAP_DIRECT;
    }
    else
    {
#ifdef XSURF100_KVMAP
	va = (volatile unsigned char *)xsurf100_kvmap(phys, XS_WINDOW, 0x40, 0);
#else
	if (phys & (NBPC - 1))
	    return ENXIO;
	va = (volatile unsigned char *)sptalloc(btoc(XS_WINDOW), PG_V, phystopfn(phys), 0);
#endif
	if (va == 0)
	    return ENOMEM;		/* no kernel VA now; the next open retries */
	info->mapped = XS_MAP_KERNEL;
    }
    info->board_base = va;
    info->nic_base = va + XS_NIC;
    return 0;
}

static void
dp_be_reset(board)
dp_board_t *board;
{
    int i;

    dp_outb(board, XS_RESET, dp_inb(board, XS_RESET));
    for (i = 0; i < 20000; i++)
	if (dp_inb(board, NE_ISR) & NE_ISR_RST)
	    break;
    dp_outb(board, NE_ISR, 0xff);
    dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STP | NE_CR_NODMA);
}

/* the PHY on: GPOC (page 0) without the power-down bit */
static void
dp_be_start(board)
dp_board_t *board;
{
    dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STP | NE_CR_NODMA);
    dp_outb(board, XS_GPOC, 0);
}

static void
dp_be_stop(board)
dp_board_t *board;
{
    dp_outb(board, XS_GPOC, XS_GPOC_PPD);
}

/* the station address PROM through remote DMA, byte wide, the NE2000 way:
 * receiver off and loopback first, 32 bytes, each byte doubled */
static void
dp_be_get_mac(board, mac)
dp_board_t *board;
unsigned char mac[6];
{
    unsigned char prom[32];
    int i, doubled = 1;

    dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STP | NE_CR_NODMA);
    dp_outb(board, NE_DCR, XS_DCR_PROM);
    dp_outb(board, NE_RBCNT0, 0);
    dp_outb(board, NE_RBCNT1, 0);
    dp_outb(board, NE_IMR, 0);
    dp_outb(board, NE_ISR, 0xff);
    dp_outb(board, NE_RCR, NE_RCR_MON);
    dp_outb(board, NE_TCR, NE_TCR_LB0);
    dp_outb(board, NE_RBCNT0, sizeof(prom));
    dp_outb(board, NE_RBCNT1, 0);
    dp_outb(board, NE_RSAR0, 0);
    dp_outb(board, NE_RSAR1, 0);
    dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STA | NE_CR_RDMA_PULL);
    for (i = 0; i < sizeof(prom); i++)
	prom[i] = *(volatile unsigned char *)XS_DATA(board);
    for (i = 0; i < 20000; i++)
	if (dp_inb(board, NE_ISR) & NE_ISR_RDC)
	    break;
    dp_outb(board, NE_ISR, NE_ISR_RDC);
    dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STP | NE_CR_NODMA);

    for (i = 0; i < 12; i += 2)
	if (prom[i] != prom[i + 1])
	    doubled = 0;
    for (i = 0; i < 6; i++)
	mac[i] = doubled ? prom[2 * i] : prom[i];

    for (i = 1; i < 6; i++)
	if (mac[i] != mac[0])
	    break;
    if (i == 6)
    {
	/* no PROM answer: whatever the AX88796B has in PAR (page 1) */
	dp_outb(board, NE_CR, NE_CR_P1 | NE_CR_STP | NE_CR_NODMA);
	for (i = 0; i < 6; i++)
	    mac[i] = dp_inb(board, NE_PAR0 + i);
	dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STP | NE_CR_NODMA);
    }
}

/* the board's own interrupt status: the level-2 line is shared */
static int
dp_be_irq_pending(board)
dp_board_t *board;
{
    return (*(volatile unsigned short *)(board->info.board_base + XS_IRQ_STATUS) &
	    0xaaaa) != 0;
}

/* the test ioctl may read and write DP8390 registers 0x00-0x0f */
static int
dp_be_test_reg_ok(board, reg, write)
dp_board_t *board;
int reg, write;
{
    return 1;
}

#include "dp8390.c"

struct streamtab xsurf100info =
{
    &dp_rinit, &dp_winit, NULL, NULL,
};

void
xsurf100intr()
{
    dp_intr();
}

/* boot: look for boards (AutoConfig only), silently */
void
xsurf100init()
{
    dp_autoconfig();
}
