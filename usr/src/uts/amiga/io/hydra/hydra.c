/*
** Hydra EtherNet driver - Hydra Systems AmigaNet (DP8390/NE2000)
**
** Zorro II Ethernet card using National Semiconductor DP8390 NIC.
** Revision 1.2a supported: 10Base2 (BNC) and 10BaseT (RJ45).
**
** The Hydra backend of the shared DP8390 core (../dp8390/dp8390.c, which
** carries the STREAMS/DLPI layer this driver had until hydra-kit 0.3): the
** NIC at board + 0xffe1 on the odd byte lane (register n at + n*2), the
** 16 KB packet buffer read and written directly at the board base, the
** station address PROM at board + 0xffc0 (every other byte).  Global
** symbols: hydrainfo (cdevsw[47].d_str), hydraintr (level 2), hydrainit.
*/

#include "sys/types.h"
#include "sys/param.h"
#include "sys/signal.h"
#include "sys/errno.h"
#include "sys/psw.h"
#include "sys/pcb.h"
#include "sys/user.h"
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
#include "hydrauser.h"

#define DP_NAME		"hya"
#define DP_MODID	0x6879
#define DP_IOC		HYDRAIOC
#define DP_MAXBOARDS	4
#define DP_BOARD_ID	0x08490001	/* manufacturer 0x0849 (2121), product 1 */
#define DP_RAM_START	0x00		/* 16 KB buffer RAM: pages 0x00..0x3f */
#define DP_TX_START	0x00
#define DP_RX_START	0x08
#define DP_STOP		0x40
#define DP_DCR		(NE_DCR_WTS | NE_DCR_BOS | NE_DCR_LS | NE_DCR_FT0)
#define DP_RCR		(NE_RCR_AB | NE_RCR_AM)

typedef hydra_status_t	dp_status_t;
typedef hydra_test_t	dp_test_t;
typedef hydra_config_t	dp_config_t;

#include "dp8390.h"

#define HYDRA_NIC_OFFSET	0xffe1
#define HYDRA_PROM_OFFSET	0xffc0

/*
** NIC_DELAY - ensure minimum chip-select deassertion time between
** DP8390 register accesses.  The AmigaOS driver reads CIA registers
** twice for this (CIA access is ~1 us on a standard Amiga).
*/
#define NIC_DELAY()	DELAY(5)

static unsigned char
dp_inb(board, reg)
dp_board_t *board;
int reg;
{
    return *(board->info.nic_base + reg * 2);
}

static void
dp_outb(board, reg, val)
dp_board_t *board;
int reg, val;
{
    *(board->info.nic_base + reg * 2) = val;
    NIC_DELAY();
}

/* the buffer RAM, a longword at a time; offset and the end within it */
static int
hydra_ram_ok(len, offset, access_len)
int len, offset, access_len;
{
    return len >= 0 && offset >= (DP_RAM_START << 8) &&
	   offset + access_len <= (DP_STOP << 8);
}

static int
dp_be_ram_write(board, buf, len, offset)
dp_board_t *board;
unsigned char *buf;
int len, offset;
{
    volatile unsigned long *dst =
	(volatile unsigned long *)(board->info.board_base + offset);
    int i, j, n, nlongs = (len + 3) >> 2;
    unsigned long v;

    if (!hydra_ram_ok(len, offset, nlongs << 2))
	return 0;
    for (i = 0; i < nlongs; i++)
    {
	v = 0;
	for (j = 0; j < 4; j++)
	{
	    n = (i << 2) + j;
	    v <<= 8;
	    if (n < len)
		v |= buf[n];
	}
	dst[i] = v;
    }
    return 1;
}

static int
dp_be_ram_read(board, buf, len, offset)
dp_board_t *board;
unsigned char *buf;
int len, offset;
{
    volatile unsigned long *src =
	(volatile unsigned long *)(board->info.board_base + offset);
    int i, j, n, nlongs = (len + 3) >> 2;
    unsigned long v;

    if (!hydra_ram_ok(len, offset, nlongs << 2))
	return 0;
    for (i = 0; i < nlongs; i++)
    {
	v = src[i];
	for (j = 3; j >= 0; j--)
	{
	    n = (i << 2) + (3 - j);
	    if (n < len)
		buf[n] = (unsigned char)((v >> (j << 3)) & 0xff);
	}
    }
    return 1;
}

/* AutoConfig: only 64 KB-aligned Zorro II addresses (the 2.1p2 table can
 * hold corrupt entries) */
static int
dp_be_addr_ok(addr, size)
long addr, size;
{
    return (addr & 0xFFFF) == 0 &&
	   ((addr >= 0x00E80000L && addr < 0x00F00000L) ||
	    (addr >= 0x00200000L && addr < 0x00A00000L));
}

/* Zorro II is inside the kernel's identity-mapped low 1 GB */
static int
dp_be_map(board)
dp_board_t *board;
{
    board->info.board_base = (volatile unsigned char *)board->info.board_phys;
    board->info.nic_base = board->info.board_base + HYDRA_NIC_OFFSET;
    board->info.mapped = 0;
    return 0;
}

static void
dp_be_reset(board)
dp_board_t *board;
{
    int i;

    dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STP | NE_CR_RDMA_ABORT);
    for (i = 0; i < 100; i++)
    {
	DELAY(1);
	if (dp_inb(board, NE_ISR) & NE_ISR_RST)
	    break;
    }
    dp_outb(board, NE_ISR, 0xff);
    for (i = 0; i < 100; i++)
    {
	DELAY(1);
	if (!(dp_inb(board, NE_ISR) & NE_ISR_RST))
	    break;
    }
    DELAY(100);
}

static void
dp_be_start(board)
dp_board_t *board;
{
}

static void
dp_be_stop(board)
dp_board_t *board;
{
}

static void
dp_be_get_mac(board, mac)
dp_board_t *board;
unsigned char mac[6];
{
    volatile unsigned char *prom = board->info.board_base + HYDRA_PROM_OFFSET;
    int j, valid = 1;

    /* stop the NIC before the PROM access (required by the Hydra) */
    dp_outb(board, NE_CR, NE_CR_STP | NE_CR_NODMA);
    dp_outb(board, NE_IMR, 0);
    dp_outb(board, NE_ISR, 0xFF);

    for (j = 0; j < 6; j++)
	mac[j] = prom[j * 2];

    /* reject all-0xFF, all-0x00, all the same byte */
    for (j = 1; j < 6; j++)
	if (mac[j] != mac[0])
	    break;
    if (j == 6)
	valid = 0;

    if (!valid)
    {
	/* fall back to the DP8390's PAR registers (page 1) */
	dp_outb(board, NE_CR, NE_CR_P1 | NE_CR_NODMA);
	for (j = 0; j < 6; j++)
	{
	    mac[j] = dp_inb(board, NE_PAR0 + j);
	    NIC_DELAY();
	}
	dp_outb(board, NE_CR, NE_CR_NODMA);
    }
}

/* the Hydra has no interrupt status of its own: ask the NIC */
static int
dp_be_irq_pending(board)
dp_board_t *board;
{
    return 1;
}

/*
** The Hydra can bus trap on a register 6 (FIFO/TBCR1) access, and on a TPSR
** or TBCR0 write while the NIC is stopped (the working SANA-II send path
** writes the transmit registers while started): those are refused, or the
** NIC is started first.
*/
static int
dp_be_test_reg_ok(board, reg, write)
dp_board_t *board;
int reg, write;
{
    if (reg == NE_FIFO)			/* = NE_TBCNT1_W */
	return 0;
    if (write && (reg == NE_TPSR || reg == NE_TBCNT0_W))
	dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STA | NE_CR_RDMA_ABORT);
    return 1;
}

#include "dp8390.c"

struct streamtab hydrainfo =
{
    &dp_rinit, &dp_winit, NULL, NULL,
};

void
hydraintr()
{
    dp_intr();
}

/* boot: look for boards (AutoConfig only), silently */
void
hydrainit()
{
    dp_autoconfig();
}
