/*
** Hydra EtherNet driver - Hydra Systems AmigaNet (DP8390/NE2000)
**
** Zorro II Ethernet card using National Semiconductor DP8390 NIC.
** Revision 1.2a supported: 10Base2 (BNC) and 10BaseT (RJ45).
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
#include "hydra.h"
#include "ne2000.h"


extern struct ifstats *ifstats;

extern char *panicstr;
void hydraautoconfig();
void hydrainit();

/* AutoConfig ROM nibble decoding constants */
#define AC_TYPE    0
#define AC_PRODUCT 1
#define AC_FLAGS   2
#define AC_RESV    3
#define AC_MANUF   4
#define AC_SERIAL  6
#define AC_DIAGVEC 10

#define ERT_TYPEMASK  0xC0
#define ERT_ZORROII   0xC0

/* Known Zorro manufacturer:product IDs */
#define HYDRA_AC_MANUF 0x0849
#define HYDRA_AC_PROD  0x01

int hydra_initialize();
static int hydraopen(), hydraclose(), hydrawput(), hydrawsrv();
static void hydraioctl();
static int hydraproto(), setup_ne2000();
static void hydra_test_write_reg();
static void hydra_reset();
static void receive_interrupt(), transmit_interrupt();
static void toss_packet_up_stream();

static struct module_info hydra_minfo =
{
    0x6879, "hya", HYDRA_MIN, HYDRA_MAX, HYDRA_HIWAT, HYDRA_LOWAT,
};

static struct qinit hydrarinit =
{
    NULL, NULL, hydraopen, hydraclose, NULL, &hydra_minfo, NULL,
};

static struct qinit hydrawinit =
{
    hydrawput, hydrawsrv, NULL, NULL, NULL, &hydra_minfo, NULL,
};

struct streamtab hydrainfo =
{
    &hydrarinit, &hydrawinit, NULL, NULL,
};

static hydra_board_t hydra_board[HYDRA_MAXBOARDS];
static hydra_autoconfig_t hydra_autoconfig[HYDRA_MAXBOARDS];
static int hydra_number_of_boards;

/*
** NIC_DELAY - ensure minimum chip-select deassertion time between
** DP8390 register accesses.  The AmigaOS driver reads CIA registers
** twice for this (CIA access is ~1 us on a standard Amiga).
*/
#define NIC_DELAY()	DELAY(5)

static unsigned char
hydra_inb(nic_base, reg)
volatile unsigned char *nic_base;
register int reg;
{
    return *(nic_base + (reg * 2));
}

static void
hydra_outb(nic_base, reg, val)
volatile unsigned char *nic_base;
int reg;
unsigned char val;
{
    *(nic_base + (reg * 2)) = val;
    NIC_DELAY();
}

static void
hydra_ram_write_offset(board, buf, len, offset)
hydra_board_t *board;
unsigned char *buf;
int len, offset;
{
    volatile unsigned long *dst =
	(volatile unsigned long *)(board->hydra_info.board_base + offset);
    int i, nlongs, access_len;

    nlongs = (len + 3) >> 2;
    access_len = nlongs << 2;

    if (len < 0 || offset < 0 ||
	offset < (NE8390_START_PG << 8) ||
	offset + access_len > (NE8390_STOP_PG << 8))
	return;

    for (i = 0; i < nlongs; i++)
    {
	unsigned long v = 0;
	int j;

	for (j = 0; j < 4; j++)
	{
	    int n = (i << 2) + j;
	    v <<= 8;
	    if (n < len)
		v |= buf[n];
	}

	dst[i] = v;
    }
}

static void
hydra_ram_read_offset(board, buf, len, offset)
hydra_board_t *board;
unsigned char *buf;
int len, offset;
{
    volatile unsigned long *src =
	(volatile unsigned long *)(board->hydra_info.board_base + offset);
    int i, nlongs, access_len;

    nlongs = (len + 3) >> 2;
    access_len = nlongs << 2;

    if (len < 0 || offset < 0 ||
	offset < (NE8390_START_PG << 8) ||
	offset + access_len > (NE8390_STOP_PG << 8))
    {
	for (i = 0; i < len; i++)
	    buf[i] = 0;
	return;
    }

    for (i = 0; i < nlongs; i++)
    {
	unsigned long v = src[i];
	int j;

	for (j = 3; j >= 0; j--)
	{
	    int n = (i << 2) + (3 - j);
	    if (n < len)
		buf[n] = (unsigned char)((v >> (j << 3)) & 0xff);
	}
    }
}

static void
hydra_ram_write(board, buf, len, page)
hydra_board_t *board;
unsigned char *buf;
int len, page;
{
    hydra_ram_write_offset(board, buf, len, page << 8);
}

static void
hydra_ram_read(board, buf, len, page)
hydra_board_t *board;
unsigned char *buf;
int len, page;
{
    hydra_ram_read_offset(board, buf, len, page << 8);
}

static void
hydra_test_write_reg(nic, reg, val)
volatile unsigned char *nic;
int reg;
unsigned char val;
{
    if (reg < 0 || reg > 0x0f)
	return;

    if (reg == NE_TBCNT1_W)
	return;

    /*
    ** The Hydra board can trap on TBCR1/FIFO access while the NIC is stopped.
    ** The working SANA-II send path writes TX setup registers while started.
    */
    if (reg == NE_TPSR || reg == NE_TBCNT0_W || reg == NE_TBCNT1_W)
    {
	hydra_outb(nic, NE_CR, NE_CR_P0 | NE_CR_STA | NE_CR_RDMA_ABORT);
	NIC_DELAY();
    }

    hydra_outb(nic, reg, val);
}

static int
hydraopen(q, devp, flag, sflag, credp)
register queue_t *q;
dev_t *devp;
int flag, sflag;
cred_t *credp;
{
    register hydra_t *hp;
    register int minor_device, board_index, sap_index;
    hydra_board_t *board;

    if (sflag == MODOPEN || sflag == CLONEOPEN)
	return EINVAL;

    hydraautoconfig();

    minor_device = getminor(*devp);
    board_index = minor_device & 0x0F;
    sap_index = (minor_device & 0xF0) >> 4;
    board = &hydra_board[board_index];

    if (board_index >= HYDRA_MAXBOARDS)
	return ENODEV;

    if (board->hydra_status.board_state != HYDRA_BOARD_RUNNING)
    {
	if (!hydra_initialize(board_index))
	{
	    cmn_err(CE_NOTE, "hya%d: init failed", board_index);
	    return ENODEV;
	}
    }

    if (board->hydra_info.board_base == 0)
    {
	cmn_err(CE_NOTE, "hya%d: no board base", board_index);
	return ENXIO;
    }

    if (sap_index == 0)
    {
	for (sap_index = 0, hp = board->hydra;
	     sap_index < HYDRA_MAXDEV;
	     sap_index++, hp++)
	{
	    if (!hp->q)
		break;
	}
    }
    else
	--sap_index;

    if (sap_index >= HYDRA_MAXDEV)
	return ENOSPC;

    *devp = makedevice(getmajor(*devp), (((sap_index + 1) << 4) | board_index));

    hp = &board->hydra[sap_index];

    if (hp->q)
	return EBUSY;

    q->q_ptr = (char *)hp;
    WR(q)->q_ptr = (char *)hp;
    hp->q = q;
    hp->board_index = board_index;
    hp->flags = 0;
    hp->state = DL_UNATTACHED;
    hp->sap = 0;

    if (!board->ifstats.ifs_name)
    {
	board->ifstats.ifs_name = "hya";
	board->ifstats.ifs_mtu = HYDRA_MTU;
	board->ifstats.ifs_next = ifstats;
	ifstats = &board->ifstats;
    }
    board->ifstats.ifs_unit = board_index;
    board->ifstats.ifs_active = 1;
    board->if_flags = IFF_BROADCAST | IFF_NOTRAILERS | IFF_RUNNING;

    return 0;
}

static int
hydraclose(q)
register queue_t *q;
{
    int dev;
    int board_index;
    register hydra_t *hp = (hydra_t *)q->q_ptr;
    register hydra_board_t *board = &hydra_board[hp->board_index];

    board_index = hp->board_index;
    hp->q = 0;

    OTHERQ(q)->q_ptr = NULL;
    q->q_ptr = NULL;

    for (dev = 0, hp = board->hydra; dev < HYDRA_MAXDEV; ++dev, ++hp)
    {
	if (hp->q)
	    break;
    }

    if (dev >= HYDRA_MAXDEV)
    {
	hydra_outb(board->hydra_info.nic_base, NE_CR,
		   NE_CR_P0 | NE_CR_STP | NE_CR_RDMA_ABORT);
	NIC_DELAY();

	board->hydra_status.board_state = HYDRA_BOARD_RESET;
	board->ifstats.ifs_unit = 0;
	board->ifstats.ifs_active = 0;
	board->ifstats.ifs_ipackets = 0;
	board->ifstats.ifs_ierrors = 0;
	board->ifstats.ifs_opackets = 0;
	board->ifstats.ifs_oerrors = 0;
    }



    return 0;
}

static int
hydrawput(q, mp)
register queue_t *q;
register mblk_t *mp;
{
    register int s;
    register hydra_t *hp = (hydra_t *)q->q_ptr;
    register hydra_board_t *board = &hydra_board[hp->board_index];

    s = splhydra();

    switch (mp->b_datap->db_type)
    {
    case M_FLUSH:
	if (*mp->b_rptr & FLUSHW)
	{
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
	(void)hydraproto(q, mp);
	break;

    case M_IOCDATA:
    case M_IOCTL:
	hydraioctl(q, mp);
	break;

    default:
	freemsg(mp);
	break;
    }
    splx(s);
}

static int
hydrawsrv()
{
    return 0;
}

static int
hydraxmit(q, mp)
queue_t *q;
mblk_t *mp;
{
    register int n, pktsize;
    register hydra_t *hp = (hydra_t *)q->q_ptr;
    hydra_board_t *board = &hydra_board[hp->board_index];
    hydra_info_t *info = &board->hydra_info;
    volatile unsigned char *nic = info->nic_base;
    unsigned char tmp[ETH_MAXPACKET + 2];
    mblk_t *oldmp;
    dl_unitdata_req_t *dp = (dl_unitdata_req_t *)mp->b_rptr;
    unsigned char *ap;

    hydra_outb(nic, NE_CR, NE_CR_P0 | NE_CR_STA | NE_CR_RDMA_ABORT);
    NIC_DELAY();
    if (hydra_inb(nic, NE_CR) & NE_CR_TXP)
    {
	NIC_DELAY();
	return 0;
    }
    NIC_DELAY();

    pktsize = sizeof(ether_header_t);
    ap = mp->b_rptr + dp->dl_dest_addr_offset;
    bcopy((caddr_t)ap, (caddr_t)&tmp[0], sizeof(ether_addr_t));
    bcopy((caddr_t)info->paddress, (caddr_t)&tmp[6], sizeof(ether_addr_t));
    bcopy((caddr_t)&hp->sap, (caddr_t)&tmp[12], sizeof(hp->sap));
    board->hydra_status.last_tx_sap = hp->sap;

    oldmp = mp;
    n = ETH_MAXDATA;

    for (mp = mp->b_cont; mp; mp = mp->b_cont)
    {
	int copy = min(mp->b_wptr - mp->b_rptr, n);
	ASSERT(copy >= 0);
	bcopy((caddr_t)mp->b_rptr, (caddr_t)&tmp[pktsize], copy);
	pktsize += copy;
	n -= copy;
    }

    freemsg(oldmp);

    if (pktsize < ETH_MINPACKET)
	pktsize = ETH_MINPACKET;

    hydra_ram_write(board, tmp, pktsize, info->tx_start_page);

    hydra_outb(nic, NE_CR, NE_CR_P0 | NE_CR_STA | NE_CR_RDMA_ABORT);
    hydra_outb(nic, NE_TPSR, info->tx_start_page);
    hydra_outb(nic, NE_TBCNT0, pktsize & 0xff);
    hydra_outb(nic, NE_TBCNT1, (pktsize >> 8) & 0xff);
    hydra_outb(nic, NE_CR, NE_CR_P0 | NE_CR_TXP | NE_CR_STA | NE_CR_RDMA_ABORT);

    board->hydra_status.packets_sent++;
    if (hp->sap == ETHERTYPE_ARP)
	board->hydra_status.tx_arp++;
    if (hp->sap == ETHERTYPE_IP)
	board->hydra_status.tx_ip++;
    board->ifstats.ifs_opackets++;

    return 1;
}

static void
hydraioctl(q, mp)
queue_t *q;
mblk_t *mp;
{
    register struct iocblk *iocbp = (struct iocblk *)mp->b_rptr;
    register hydra_t *hp = (hydra_t *)q->q_ptr;
    register hydra_board_t *board = &hydra_board[hp->board_index];

    if (mp->b_datap->db_type == M_IOCDATA)
    {
	struct copyresp *cp = (struct copyresp *)mp->b_rptr;
	if (cp->cp_rval)
	{
	    freemsg(mp);
	    return;
	}

	if (cp->cp_private)
	{
	    caddr_t user_addr = (caddr_t)cp->cp_private;
	    hydra_test_t *t = (hydra_test_t *)mp->b_cont->b_rptr;
	    volatile unsigned char *nic = board->hydra_info.nic_base;
	    int s;

	    s = splhydra();

	    switch (t->op)
	    {
	    case HYDRA_TEST_READ_REG:
		if (t->reg == NE_FIFO)
		    t->val = 0;
		else
		    t->val = hydra_inb(nic, t->reg);
		break;

	    case HYDRA_TEST_WRITE_REG:
		hydra_test_write_reg(nic, t->reg, t->val);
		break;

	    case HYDRA_TEST_READ_RAM:
		if (t->len >= 0 && t->len <= sizeof(t->data) &&
		    (t->page << 8) + t->len < NE8390_STOP_PG * 256)
		    hydra_ram_read(board, t->data, t->len, t->page);
		break;

	    case HYDRA_TEST_WRITE_RAM:
		if (t->len >= 0 && t->len <= sizeof(t->data) &&
		    (t->page << 8) + t->len < NE8390_STOP_PG * 256)
		    hydra_ram_write(board, t->data, t->len, t->page);
		break;

	    case HYDRA_TEST_GET_STATE:
		/*
		** Do not touch live NIC registers here.  Hydra boards can bus
		** trap around the FIFO/TBCR1 slot, and diagnostics must not be
		** able to panic the kernel by requesting a register snapshot.
		*/
		t->tsr = 0;
		t->isr = 0;
		t->rsr = 0;
		t->ncr = 0;
		t->tcr = 0;
		t->rcr = 0;
		t->curr = 0;
		t->bnry = 0;
		break;

	    case HYDRA_TEST_TX:
		break;

	    case HYDRA_TEST_GET_BOARD:
		t->page = (int)board->hydra_info.board_base;
		bcopy((caddr_t)board->hydra_info.paddress, (caddr_t)t->data, 6);
		t->data[6] = board->hydra_status.board_state;
		break;
	    }

	    splx(s);

	    {
		struct copyreq *creq = (struct copyreq *)mp->b_rptr;
		mp->b_datap->db_type = M_COPYOUT;
		mp->b_wptr = mp->b_rptr + sizeof(struct copyreq);
		creq->cq_addr = user_addr;
		creq->cq_size = sizeof(hydra_test_t);
		creq->cq_flag = 0;
		creq->cq_private = 0;
		putnext(RD(q), mp);
	    }
	    return;
	}

	if (!cp->cp_private)
	{
	    mp->b_datap->db_type = M_IOCACK;
	    freemsg(unlinkb(mp));
	    iocbp->ioc_count = 0;
	    iocbp->ioc_rval = 0;
	    iocbp->ioc_error = 0;
	    putnext(RD(q), mp);
	    return;
	}
    }

    switch (iocbp->ioc_cmd)
    {
    case SIOCSIFFLAGS:
    case SIOCGIFFLAGS:
	mp->b_datap->db_type = M_IOCACK;
	putnext(RD(q), mp);
	return;

    case HYDRA_NUMBER_OF_BOARDS:
    {
	caddr_t arg = *(caddr_t *)mp->b_cont->b_rptr;

	freemsg(mp->b_cont);

	mp->b_cont = allocb(sizeof(int), BPRI_MED);
	if (!mp->b_cont)
	{
	    mp->b_datap->db_type = M_IOCNAK;
	    freemsg(unlinkb(mp));
	    iocbp->ioc_count = 0;
	    iocbp->ioc_rval = 0;
	    iocbp->ioc_error = ENOMEM;
	    putnext(RD(q), mp);
	    return;
	}

	*(int *)mp->b_cont->b_rptr = hydra_number_of_boards;
	mp->b_cont->b_wptr += sizeof(int);

	if (iocbp->ioc_count == TRANSPARENT)
	{
	    struct copyreq *creq = (struct copyreq *)mp->b_rptr;
	    mp->b_datap->db_type = M_COPYOUT;
	    creq->cq_addr = arg;
	    mp->b_wptr = mp->b_rptr + sizeof *creq;
	    creq->cq_size = sizeof(int);
	    creq->cq_flag = 0;
	    creq->cq_private = (mblk_t *)0;
	}
	else
	{
	    iocbp->ioc_count = sizeof(int);
	    mp->b_datap->db_type = M_IOCACK;
	}
	putnext(RD(q), mp);
	return;
    }

    case HYDRA_GET_CONFIG:
    {
	hydra_config_t cfg;
	caddr_t arg = *(caddr_t *)mp->b_cont->b_rptr;

	cfg.board_base = (long)board->hydra_info.board_base;
	cfg.board_debug = board->hydra_status.board_debug;
	bcopy((caddr_t)board->hydra_info.paddress, (caddr_t)cfg.paddress, 6);
	bzero((caddr_t)cfg.laddress, sizeof(cfg.laddress));
	cfg.mode = 0;
	cfg.flags = 0;

	freemsg(mp->b_cont);

	mp->b_cont = allocb(sizeof(hydra_config_t), BPRI_MED);
	if (!mp->b_cont)
	{
	    mp->b_datap->db_type = M_IOCNAK;
	    freemsg(unlinkb(mp));
	    iocbp->ioc_count = 0;
	    iocbp->ioc_rval = 0;
	    iocbp->ioc_error = ENOMEM;
	    putnext(RD(q), mp);
	    return;
	}

	*(hydra_config_t *)mp->b_cont->b_rptr = cfg;
	mp->b_cont->b_wptr += sizeof(hydra_config_t);

	if (iocbp->ioc_count == TRANSPARENT)
	{
	    struct copyreq *creq = (struct copyreq *)mp->b_rptr;
	    mp->b_datap->db_type = M_COPYOUT;
	    creq->cq_addr = arg;
	    mp->b_wptr = mp->b_rptr + sizeof *creq;
	    creq->cq_size = sizeof(hydra_config_t);
	    creq->cq_flag = 0;
	    creq->cq_private = (mblk_t *)0;
	}
	else
	{
	    iocbp->ioc_count = sizeof(hydra_config_t);
	    mp->b_datap->db_type = M_IOCACK;
	}
	putnext(RD(q), mp);
	return;
    }

    case HYDRA_GET_STATUS:
    {
	hydra_status_t status;
	caddr_t arg = *(caddr_t *)mp->b_cont->b_rptr;
	int i;

	status = board->hydra_status;
	status.open_streams = 0;
	status.bound_streams = 0;
	status.arp_streams = 0;
	status.ip_streams = 0;
	for (i = 0; i < HYDRA_MAXDEV; i++)
	{
	    if (board->hydra[i].q)
	    {
		status.open_streams++;
		if (board->hydra[i].sap)
		    status.bound_streams++;
		if (board->hydra[i].sap == ETHERTYPE_ARP)
		    status.arp_streams++;
		if (board->hydra[i].sap == ETHERTYPE_IP)
		    status.ip_streams++;
	    }
	}
	status.if_active = board->ifstats.ifs_active;
	status.if_ipackets = board->ifstats.ifs_ipackets;
	status.if_opackets = board->ifstats.ifs_opackets;
	status.if_ierrors = board->ifstats.ifs_ierrors;
	status.if_oerrors = board->ifstats.ifs_oerrors;
	status.sw_next_pkt = board->hydra_info.next_pkt;

	freemsg(mp->b_cont);

	mp->b_cont = allocb(sizeof(hydra_status_t), BPRI_MED);
	if (!mp->b_cont)
	{
	    mp->b_datap->db_type = M_IOCNAK;
	    freemsg(unlinkb(mp));
	    iocbp->ioc_count = 0;
	    iocbp->ioc_rval = 0;
	    iocbp->ioc_error = ENOMEM;
	    putnext(RD(q), mp);
	    return;
	}

	*(hydra_status_t *)mp->b_cont->b_rptr = status;
	mp->b_cont->b_wptr += sizeof(hydra_status_t);

	if (iocbp->ioc_count == TRANSPARENT)
	{
	    struct copyreq *creq = (struct copyreq *)mp->b_rptr;
	    mp->b_datap->db_type = M_COPYOUT;
	    creq->cq_addr = arg;
	    mp->b_wptr = mp->b_rptr + sizeof *creq;
	    creq->cq_size = sizeof(hydra_status_t);
	    creq->cq_flag = 0;
	    creq->cq_private = (mblk_t *)0;
	}
	else
	{
	    iocbp->ioc_count = sizeof(hydra_status_t);
	    mp->b_datap->db_type = M_IOCACK;
	}
	putnext(RD(q), mp);
	return;
    }

    case HYDRA_IOCTL_TEST:
    {
	hydra_test_t *t;
	volatile unsigned char *nic;
	int s;

	if (iocbp->ioc_count == TRANSPARENT)
	{
	    struct copyreq *creq = (struct copyreq *)mp->b_rptr;
	    caddr_t arg = *(caddr_t *)mp->b_cont->b_rptr;

	    freemsg(mp->b_cont);

	    mp->b_cont = allocb(sizeof(hydra_test_t), BPRI_MED);
	    if (!mp->b_cont)
	    {
		mp->b_datap->db_type = M_IOCNAK;
		freemsg(unlinkb(mp));
		iocbp->ioc_count = 0;
		iocbp->ioc_rval = 0;
		iocbp->ioc_error = ENOMEM;
		putnext(RD(q), mp);
		return;
	    }

	    mp->b_datap->db_type = M_COPYIN;
	    mp->b_wptr = mp->b_rptr + sizeof(struct copyreq);
	    creq->cq_addr = arg;
	    creq->cq_size = sizeof(hydra_test_t);
	    creq->cq_flag = 0;
	    creq->cq_private = (mblk_t *)arg;
	    putnext(RD(q), mp);
	    return;
	}

	t = (hydra_test_t *)mp->b_cont->b_rptr;
	nic = board->hydra_info.nic_base;

	s = splhydra();

	switch (t->op)
	{
	case HYDRA_TEST_READ_REG:
	    if (t->reg == NE_FIFO)
		t->val = 0;
	    else
		t->val = hydra_inb(nic, t->reg);
	    break;

	case HYDRA_TEST_WRITE_REG:
	    hydra_test_write_reg(nic, t->reg, t->val);
	    break;

	case HYDRA_TEST_READ_RAM:
	    if (t->len >= 0 && t->len <= sizeof(t->data) &&
		(t->page << 8) + t->len < NE8390_STOP_PG * 256)
		hydra_ram_read(board, t->data, t->len, t->page);
	    break;

	case HYDRA_TEST_WRITE_RAM:
	    if (t->len >= 0 && t->len <= sizeof(t->data) &&
		(t->page << 8) + t->len < NE8390_STOP_PG * 256)
		hydra_ram_write(board, t->data, t->len, t->page);
	    break;

	case HYDRA_TEST_GET_STATE:
	    /*
	    ** Do not touch live NIC registers here.  Hydra boards can bus
	    ** trap around the FIFO/TBCR1 slot, and diagnostics must not be
	    ** able to panic the kernel by requesting a register snapshot.
	    */
	    t->tsr = 0;
	    t->isr = 0;
	    t->rsr = 0;
	    t->ncr = 0;
	    t->tcr = 0;
	    t->rcr = 0;
	    t->curr = 0;
	    t->bnry = 0;
	    break;

	case HYDRA_TEST_TX:
	    break;

	case HYDRA_TEST_GET_BOARD:
	    t->page = (int)board->hydra_info.board_base;
	    bcopy((caddr_t)board->hydra_info.paddress, (caddr_t)t->data, 6);
	    t->data[6] = board->hydra_status.board_state;
	    break;
	}

	splx(s);

	iocbp->ioc_count = sizeof(hydra_test_t);
	mp->b_datap->db_type = M_IOCACK;
	putnext(RD(q), mp);
	return;
    }

    case IF_UNITSEL:
    default:
	mp->b_datap->db_type = M_IOCNAK;
	freemsg(unlinkb(mp));
	iocbp->ioc_count = 0;
	iocbp->ioc_rval = 0;
	iocbp->ioc_error = EINVAL;
	putnext(RD(q), mp);
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

static int
hydraproto(q, mp)
queue_t *q;
mblk_t *mp;
{
    register union DL_primitives *p = (union DL_primitives *)mp->b_rptr;
    register hydra_t *hp = (hydra_t *)q->q_ptr;
    register hydra_board_t *board = &hydra_board[hp->board_index];

    switch (p->dl_primitive)
    {
    case DL_UNITDATA_REQ:
	if (q->q_first || !hydraxmit(q, mp))
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

	    hp->state = DL_UNBOUND;

	    qreply(q, mp);
	    return 1;
	}

    case DL_BIND_REQ:
	{
	    dl_bind_req_t *reqp = (dl_bind_req_t *)mp->b_rptr;
	    dl_bind_ack_t *ackp;

	    hp->sap = reqp->dl_sap;
	    board->hydra_status.last_bound_sap = hp->sap;
	    hp->state = DL_IDLE;

	    freemsg(mp);

	    if (!(mp = allocb(sizeof(*ackp) + sizeof(board->hydra_info.paddress),
			    BPRI_MED)))
		return 1;

	    mp->b_datap->db_type = M_PCPROTO;
	    ackp = (dl_bind_ack_t *)mp->b_wptr;
	    ackp->dl_primitive = DL_BIND_ACK;
	    ackp->dl_sap = hp->sap;
	    ackp->dl_addr_length = sizeof(board->hydra_info.paddress);
	    ackp->dl_addr_offset = sizeof(*ackp);
	    ackp->dl_max_conind = 0;
	    ackp->dl_growth = 0;
	    mp->b_wptr += sizeof(*ackp);

	    bcopy((caddr_t)board->hydra_info.paddress, (caddr_t)mp->b_wptr,
		  sizeof(board->hydra_info.paddress));

	    mp->b_wptr += sizeof(board->hydra_info.paddress);

	    qreply(q, mp);
	    return 1;
	}

    case DL_INFO_REQ:
	{
	    dl_info_ack_t *ackp;

	    freemsg(mp);

	    if (!(mp = allocb(sizeof(dl_info_ack_t) +
			      sizeof(board->hydra_info.paddress), BPRI_MED)))
		return 1;

	    mp->b_datap->db_type = M_PCPROTO;
	    ackp = (dl_info_ack_t *)mp->b_wptr;
	    ackp->dl_primitive = DL_INFO_ACK;
	    ackp->dl_max_sdu = ETH_MAXDATA;
	    ackp->dl_min_sdu = 1;
	    ackp->dl_addr_length = sizeof(board->hydra_info.paddress);
	    ackp->dl_mac_type = DL_ETHER;
	    ackp->dl_reserved = 0;
	    ackp->dl_current_state = hp->state;
	    ackp->dl_max_idu = ETH_MAXDATA;
	    ackp->dl_service_mode = DL_CLDLS;
	    ackp->dl_qos_length = 0;
	    ackp->dl_qos_offset = 0;
	    ackp->dl_qos_range_length = 0;
	    ackp->dl_qos_range_offset = 0;
	    ackp->dl_provider_style = DL_STYLE1;
	    ackp->dl_growth = 0;
	    if (hp->state == DL_IDLE)
	    {
		ackp->dl_addr_offset = sizeof(dl_info_ack_t);
		mp->b_wptr += sizeof(dl_info_ack_t);
		bcopy((caddr_t)board->hydra_info.paddress,
		      (caddr_t)mp->b_wptr,
		      sizeof(board->hydra_info.paddress));
		mp->b_wptr += sizeof(board->hydra_info.paddress);
	    }
	    else
	    {
		ackp->dl_addr_offset = 0;
		mp->b_wptr += sizeof(dl_info_ack_t);
	    }

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

	    hp->state = DL_UNBOUND;
	    hp->sap = 0;

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

	    hp->state = DL_UNATTACHED;

	    qreply(q, mp);
	    return 1;
	}

    default:
	freemsg(mp);
	return 1;
    }

    return 0;
}

hydraintr()
{
    unsigned char status;
    int board_index;

    if (panicstr)
	return;

    for (board_index = 0; board_index < HYDRA_MAXBOARDS; ++board_index)
    {
	hydra_board_t *board = &hydra_board[board_index];

	if (board->hydra_status.board_state == HYDRA_BOARD_RUNNING &&
	    board->hydra_info.nic_base != 0)
	{
	    volatile unsigned char *nic = board->hydra_info.nic_base;

	hydra_outb(nic, NE_CR, NE_CR_P0 | NE_CR_STA | NE_CR_RDMA_ABORT);
	    NIC_DELAY();
	    status = hydra_inb(nic, NE_ISR);

	    if (status == 0)
		continue;

	    if (status & (NE_ISR_PRX | NE_ISR_RXE | NE_ISR_OVW))
	    {
		if (status & NE_ISR_RXE)
		{
		    board->hydra_status.rx_errors++;
		    board->ifstats.ifs_ierrors++;
		}

		if (status & NE_ISR_OVW)
		{
		    board->hydra_status.overflow++;
		    board->ifstats.ifs_ierrors++;
		}

		hydra_outb(nic, NE_ISR,
			   status & (NE_ISR_PRX | NE_ISR_RXE | NE_ISR_OVW));
		NIC_DELAY();

		if (status & NE_ISR_PRX)
		    receive_interrupt(board_index);
	    }

	    if (status & NE_ISR_PTX)
	    {
		hydra_outb(nic, NE_ISR, NE_ISR_PTX);
		NIC_DELAY();
		transmit_interrupt(board_index);
	    }

	    if (status & NE_ISR_TXE)
	    {
		unsigned char tsr;
		hydra_outb(nic, NE_ISR, NE_ISR_TXE);
		NIC_DELAY();
		tsr = hydra_inb(nic, NE_TSR);
		NIC_DELAY();
		board->hydra_status.tx_errors++;
		board->ifstats.ifs_oerrors++;
		if (tsr & NE_TSR_COL)
		    board->hydra_status.collisions++;
		if (tsr & NE_TSR_ABT)
		    board->hydra_status.retry_errors++;
		if (tsr & NE_TSR_CDH)
		    board->hydra_status.late_collisions++;
		if (tsr & NE_TSR_CRS)
		    board->hydra_status.loss_of_carrier++;
	    }

	    if (status & NE_ISR_CNT)
	    {
		hydra_outb(nic, NE_ISR, NE_ISR_CNT);
		NIC_DELAY();
		board->hydra_status.missed_packets += hydra_inb(nic, NE_CNTR0);
	    }
	}
    }
}

static void
receive_interrupt(board_index)
int board_index;
{
    hydra_board_t *board = &hydra_board[board_index];
    hydra_info_t *info = &board->hydra_info;
    volatile unsigned char *nic = info->nic_base;
    static unsigned char rxbuff[MAX_BUFFER_LENGTH];
    int curr, boundary, pktsize;
    unsigned char rsr;
    int next_frame;
    int loop_count = 0;

    hydra_outb(nic, NE_CR, NE_CR_P0 | NE_CR_STA | NE_CR_RDMA_ABORT);
    NIC_DELAY();

    while (1)
    {
	if (++loop_count > 256)
	{
	    board->hydra_status.rx_errors++;
	    break;
	}

	hydra_outb(nic, NE_CR, NE_CR_P1 | NE_CR_STA | NE_CR_RDMA_ABORT);
	NIC_DELAY();
	curr = hydra_inb(nic, NE_CURR);
	NIC_DELAY();
	hydra_outb(nic, NE_CR, NE_CR_P0 | NE_CR_STA | NE_CR_RDMA_ABORT);
	NIC_DELAY();
	boundary = hydra_inb(nic, NE_BNRY);
	NIC_DELAY();
	board->hydra_status.last_curr = curr;
	board->hydra_status.last_bnry = boundary;

	next_frame = info->next_pkt;

	if (next_frame == curr)
	    break;

	{
	    unsigned char header[4];
	    int header_page = next_frame;

	    hydra_ram_read(board, header, 4, header_page);

	    board->hydra_status.last_ring_page = header_page;
	    board->hydra_status.last_ring_next = header[0];
	    board->hydra_status.last_ring_rsr = header[1];
	    board->hydra_status.last_ring_size =
		((unsigned int)header[3] << 8) | header[2];

	    next_frame = header[0];
	    rsr = header[1];
	    pktsize = ((unsigned int)header[3] << 8) | header[2];
	}

	if (next_frame < NE8390_RX_START_PG || next_frame >= NE8390_STOP_PG)
	{
	    board->hydra_status.rx_errors++;
	    board->hydra_status.buffer_error++;
	    board->hydra_status.rx_bad_next++;
	    board->ifstats.ifs_ierrors++;
	    info->next_pkt = NE8390_RX_START_PG;
	    {
		hydra_outb(nic, NE_BNRY, NE8390_RX_START_PG);
		NIC_DELAY();
	    }
	    break;
	}

	if (!(rsr & NE_RSR_PRX))
	{
	    board->hydra_status.rx_errors++;
	    board->hydra_status.rx_bad_rsr++;
	    board->ifstats.ifs_ierrors++;
	    info->next_pkt = next_frame;
	    {
		int bnry = next_frame - 1;
		if (bnry < NE8390_RX_START_PG)
		    bnry = NE8390_STOP_PG - 1;
		hydra_outb(nic, NE_BNRY, bnry);
		NIC_DELAY();
	    }
	    continue;
	}

	pktsize -= 4;

	if (pktsize < ETH_MINFRAME || pktsize > MAX_BUFFER_LENGTH)
	{
	    board->hydra_status.buffer_error++;
	    board->hydra_status.rx_bad_size++;
	    info->next_pkt = next_frame;
	    {
		int bnry = next_frame - 1;
		if (bnry < NE8390_RX_START_PG)
		    bnry = NE8390_STOP_PG - 1;
		hydra_outb(nic, NE_BNRY, bnry);
		NIC_DELAY();
	    }
	    continue;
	}

	{
	    int offset = info->next_pkt * 256 + 4;
	    int stop = NE8390_STOP_PG * 256;

	    if (offset + pktsize > stop)
	    {
		int semi = stop - offset;
		hydra_ram_read_offset(board, rxbuff, semi, offset);
		hydra_ram_read_offset(board, rxbuff + semi,
				      pktsize - semi,
				      info->rx_start_page << 8);
	    }
	    else
		hydra_ram_read_offset(board, rxbuff, pktsize, offset);
	}

	board->hydra_status.packets_received++;
	board->ifstats.ifs_ipackets++;

	if (rsr & NE_RSR_CRC)
	    board->hydra_status.crc_errors++;
	if (rsr & NE_RSR_FAE)
	    board->hydra_status.framing_errors++;
	if (rsr & NE_RSR_FO)
	    board->hydra_status.overflow++;

	info->next_pkt = next_frame;
	{
	    int bnry = next_frame - 1;
	    if (bnry < NE8390_RX_START_PG)
		bnry = NE8390_STOP_PG - 1;
	    hydra_outb(nic, NE_BNRY, bnry);
	    NIC_DELAY();
	}

	toss_packet_up_stream(rxbuff, board_index, pktsize);
    }
}

static void
toss_packet_up_stream(packet, board_index, count)
char *packet;
int board_index, count;
{
    int j, length, trailn;
    unsigned short sap;
    hydra_board_t *board = &hydra_board[board_index];
    hydra_info_t *info = &board->hydra_info;
    hydra_t *hp = board->hydra;
    static unsigned char broadcast_address[6] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };
    ether_header_t *packet_header = (ether_header_t *)packet;

    sap = packet_header->ether_type;
    board->hydra_status.last_rx_sap = sap;
    if (sap == ETHERTYPE_ARP)
	board->hydra_status.rx_arp_seen++;
    if (sap == ETHERTYPE_IP)
	board->hydra_status.rx_ip_seen++;

    if (sap >= ETHERTYPE_TRAIL &&
	sap <= (unsigned short)(ETHERTYPE_TRAIL + ETHERTYPE_NTRAILER))
    {
	length = (sap - ETHERTYPE_TRAIL) * 512;
	trailn = (*((unsigned short *)
		    &packet[sizeof(ether_header_t) + length + ETH_TYPE_SIZE]) -
		  ETH_TYPE_SIZE - ETH_LENGTH_SIZE);
	sap = *((unsigned short *)&packet[sizeof(ether_header_t) + length]);
	board->hydra_status.trailers++;
    }
    else
    {
	length = count - sizeof(ether_header_t);
	trailn = 0;
    }

    {
	int sap_matched = 0;

    for (j = 0; j < HYDRA_MAXDEV; j++, hp++)
    {
	mblk_t *mp = (mblk_t *)NULL;

	if (!hp->q)
	    continue;

	if (hp->flags & HYDRA_RAW_DATA)
	{
	    if (canput(hp->q->q_next))
	    {
		if ((mp = allocb(sizeof(hydra_status_t), BPRI_MED)) != NULL &&
		    (mp->b_cont = allocb(count, BPRI_MED)) != NULL)
		{
		    mp->b_datap->db_type = M_PROTO;
		    bcopy((caddr_t)&board->hydra_status,
			  (caddr_t)mp->b_wptr, sizeof(board->hydra_status));
		    mp->b_wptr += sizeof(board->hydra_status);

		    mp->b_cont->b_datap->db_type = M_DATA;
		    bcopy((caddr_t)packet, (caddr_t)mp->b_cont->b_wptr, count);
		    mp->b_cont->b_wptr += count;
		    putnext(hp->q, mp);
		}
		else
		{
		    board->hydra_status.allocbs_failed++;
		    if (mp)
			freemsg(mp);
		}
	    }
	    else
		board->hydra_status.couldnt_put++;
	}
	else if (hp->sap == sap)
	{
	    sap_matched = 1;

	    if (bcmp((char *)packet_header->ether_dhost,
		     (char *)info->paddress,
		     sizeof(ether_addr_t)) &&
		bcmp((char *)packet_header->ether_dhost,
		     (char *)broadcast_address,
		     sizeof(broadcast_address)))
	    {
		board->hydra_status.rx_not_for_us++;
		continue;
	    }

	    if (canput(hp->q->q_next))
	    {
		if ((mp = allocb(sizeof(union DL_primitives) +
			       (2 * sizeof(ether_addr_t)),
			       BPRI_MED)) != NULL &&
		    (mp->b_cont = allocb(length + trailn, BPRI_MED)) != NULL)
		{
		    register dl_unitdata_ind_t
			*dp = (dl_unitdata_ind_t *)mp->b_wptr;

		    dp->dl_primitive = DL_UNITDATA_IND;
		    dp->dl_dest_addr_length = sizeof(ether_addr_t);
		    dp->dl_dest_addr_offset = sizeof(union DL_primitives);
		    dp->dl_src_addr_length = sizeof(ether_addr_t);
		    dp->dl_src_addr_offset = sizeof(union DL_primitives) +
			sizeof(ether_addr_t);

		    bcopy((caddr_t)&packet_header->ether_dhost,
			  (caddr_t)mp->b_wptr + dp->dl_dest_addr_offset,
			  sizeof(ether_addr_t));

		    bcopy((caddr_t)&packet_header->ether_shost,
			  (caddr_t)mp->b_wptr + dp->dl_src_addr_offset,
			  sizeof(ether_addr_t));

		    dp->dl_reserved = 0;
		    mp->b_wptr += sizeof(union DL_primitives) +
			(2 * sizeof(ether_addr_t));

		    mp->b_datap->db_type = M_PROTO;

		    if (trailn)
		    {
			bcopy((caddr_t)
			      &packet[sizeof(ether_header_t) + length +
				      ETH_TYPE_SIZE + ETH_LENGTH_SIZE],
			      (caddr_t)mp->b_cont->b_wptr,
			      trailn);

			mp->b_cont->b_wptr += trailn;
		    }

		    bcopy((caddr_t)&packet[sizeof(ether_header_t)],
			  (caddr_t)mp->b_cont->b_wptr,
			  length);

		    mp->b_cont->b_wptr += length;
		    mp->b_cont->b_datap->db_type = M_DATA;

		    putnext(hp->q, mp);
		    board->hydra_status.rx_delivered++;
		    if (sap == ETHERTYPE_ARP)
			board->hydra_status.rx_arp_delivered++;
		    if (sap == ETHERTYPE_IP)
			board->hydra_status.rx_ip_delivered++;
		}
		else
		{
		    board->hydra_status.allocbs_failed++;
		    if (mp)
			freemsg(mp);
		}
	    }
	    else
		board->hydra_status.couldnt_put++;
	}
    }

    if (!sap_matched)
	board->hydra_status.rx_no_sap++;
    }
}

static void
transmit_interrupt(board_index)
int board_index;
{
    hydra_t *hp = hydra_board[board_index].hydra;
    int n;
    mblk_t *mp;

    for (n = 0; n < HYDRA_MAXDEV; n++, hp++)
    {
	if (hp->q)
	{
	    queue_t *wrq = WR(hp->q);

	    mp = getq(wrq);

	    if (mp && !hydraxmit(wrq, mp))
	    {
		putbq(wrq, mp);
		break;
	    }
	}
    }
}

static void
get_ethernet_address(base, physical_ethernet_address)
long base;
unsigned char physical_ethernet_address[6];
{
    volatile unsigned char *nic;
    int j, valid;

    nic = (volatile unsigned char *)(base + NE8390_NIC_OFFSET);

    /* Stop NIC before PROM access (required by Hydra hardware) */
    hydra_outb(nic, NE_CR, NE_CR_STP | NE_CR_NODMA);
    NIC_DELAY();
    hydra_outb(nic, NE_IMR, 0);
    NIC_DELAY();
    hydra_outb(nic, NE_ISR, 0xFF);
    NIC_DELAY();

    /* Method 1: read MAC from PROM at $FFC0 step-2 */
    {
	register volatile unsigned char *ptr;
	ptr = (unsigned char *)(base + NE8390_ADDRPROM_OFFSET);
	for (j = 0; j < 6; j++)
	    physical_ethernet_address[j] = *(ptr + j * 2);
    }

    /* Validate: reject all-0xFF, all-0x00, or all-same-byte */
    valid = 1;
    for (j = 0; j < 6; j++)
	if (physical_ethernet_address[j] != 0xFF) break;
    if (j == 6) valid = 0;
    for (j = 0; j < 6; j++)
	if (physical_ethernet_address[j] != 0x00) break;
    if (j == 6) valid = 0;
    for (j = 1; j < 6; j++)
	if (physical_ethernet_address[j] != physical_ethernet_address[0]) break;
    if (j == 6) valid = 0;

	if (!valid)
	{
	    /* Method 2: fallback ? read MAC from DP8390 PAR registers (page 1) */
	    hydra_outb(nic, NE_CR, NE_CR_P1 | NE_CR_NODMA);
	    NIC_DELAY();
	    for (j = 0; j < 6; j++)
	    {
		physical_ethernet_address[j] = hydra_inb(nic, NE_PAR0 + j);
		NIC_DELAY();
	    }
	    hydra_outb(nic, NE_CR, NE_CR_NODMA);
	    NIC_DELAY();
	}
}

/* AutoConfig nibble decode helpers */
static unsigned char
ac_byte(volatile unsigned char *mem, int n)
{
    unsigned char hi = (mem[n * 4    ] >> 4) & 0x0F;
    unsigned char lo = (mem[n * 4 + 2] >> 4) & 0x0F;
    unsigned char raw = (hi << 4) | lo;
    return (n == AC_TYPE) ? raw : (unsigned char)(~raw & 0xFF);
}

static unsigned short
ac_word(volatile unsigned char *mem, int n)
{
    return (unsigned short)((unsigned short)ac_byte(mem, n) << 8 | ac_byte(mem, n + 1));
}

static int
ac_match_board(volatile unsigned char *mem)
{
    unsigned char er_type;
    int i;

    /* Reject bus float */
    for (i = 0; i < 16; i++)
	if (mem[i] != 0xFF) break;
    if (i == 16) return 0;
    for (i = 0; i < 16; i++)
	if (mem[i] != 0x00) break;
    if (i == 16) return 0;

    /* Must be Zorro II type */
    er_type = ac_byte(mem, AC_TYPE);
    if ((er_type & ERT_TYPEMASK) != ERT_ZORROII)
	return 0;

    /* Reserved byte must decode to 0x00 */
    if (ac_byte(mem, AC_RESV) != 0x00)
	return 0;

    /* Check manufacturer and product */
    {
	unsigned short manuf = ac_word(mem, AC_MANUF);
	unsigned char prod   = ac_byte(mem, AC_PRODUCT);
	return (manuf == HYDRA_AC_MANUF && prod == HYDRA_AC_PROD);
    }
}

void
hydraautoconfig()
{
    int i, n, slot;
    long addr, size;
    static int hydraautoconfigured;

    if (hydraautoconfigured)
	return;

    hydraautoconfigured = 1;
    hydra_number_of_boards = 0;

    /* ---- Run all probe methods, use first that finds a valid MAC ---- */

    /* Method 1: autocon() from bootinfo.autocon[] ConfigDev table */
    {
	int n1 = 0;
	for (i = 0; i < HYDRA_MAXBOARDS; i++)
	{
	    addr = 0; size = 0;
	    if (!autocon(NE8390_BOARD_ID, i, &addr, &size))
		break;

	    /* Accept only 64KB-aligned Zorro II addresses */
	    if ((addr & 0xFFFF) == 0 &&
		((addr >= 0x00E80000UL && addr < 0x00F00000UL) ||
		 (addr >= 0x00200000UL && addr < 0x00A00000UL)))
	    {
		hydra_autoconfig[hydra_number_of_boards].address = addr;
		hydra_autoconfig[hydra_number_of_boards].type = 1;
		hydra_number_of_boards++;
		n1++;
	    }
	}
	if (n1 > 0)
	{
	    return;
	}
    }

    /* Method 2: Direct PROM probe at Zorro II I/O slots (0xE90000-0xEFFFFF) */
    n = 0;
    for (slot = 0; slot < 7; slot++)
    {
	unsigned long base = 0x00E90000UL + slot * 0x10000UL;
	unsigned char mac[6];
	int valid;

	for (i = 0; i < 6; i++)
	    mac[i] = *(volatile unsigned char *)(base + NE8390_ADDRPROM_OFFSET + i * 2);

	valid = 1;
	for (i = 0; i < 6; i++)
	    if (mac[i] != 0xFF) break;
	if (i == 6) valid = 0;
	for (i = 0; i < 6; i++)
	    if (mac[i] != 0x00) break;
	if (i == 6) valid = 0;
	for (i = 1; i < 6; i++)
	    if (mac[i] != mac[0]) break;
	if (i == 6) valid = 0;

	if (!valid)
	    continue;

	if (hydra_number_of_boards < HYDRA_MAXBOARDS)
	{
	    hydra_autoconfig[hydra_number_of_boards].address = base;
	    hydra_autoconfig[hydra_number_of_boards].type = 1;
	    hydra_number_of_boards++;
	    n++;
	}
    }
    if (n > 0)
    {
	return;
    }

    /* Method 3: Direct PROM probe at Zorro II memory space (0x200000-0x9FFFFF) */
    n = 0;
    for (addr = 0x00200000; addr < 0x00A00000; addr += 0x00010000)
    {
	unsigned char mac[6];
	int valid;

	for (i = 0; i < 6; i++)
	    mac[i] = *(volatile unsigned char *)(addr + NE8390_ADDRPROM_OFFSET + i * 2);

	valid = 1;
	for (i = 0; i < 6; i++)
	    if (mac[i] != 0xFF) break;
	if (i == 6) valid = 0;
	for (i = 0; i < 6; i++)
	    if (mac[i] != 0x00) break;
	if (i == 6) valid = 0;
	for (i = 1; i < 6; i++)
	    if (mac[i] != mac[0]) break;
	if (i == 6) valid = 0;

	if (!valid)
	    continue;

	if (hydra_number_of_boards < HYDRA_MAXBOARDS)
	{
	    hydra_autoconfig[hydra_number_of_boards].address = addr;
	    hydra_autoconfig[hydra_number_of_boards].type = 1;
	    hydra_number_of_boards++;
	    n++;
	}
    }
    if (n > 0)
    {
	return;
    }

    cmn_err(CE_NOTE, "hydra: no board found");
}

int
hydra_initialize(board_index)
int board_index;
{
    int n;
    hydra_t *hp;
    unsigned char paddress[6];
    hydra_board_t *board = &hydra_board[board_index];

    board->hydra_info.board_base =
	(unsigned char *)hydra_autoconfig[board_index].address;
    if (board->hydra_info.board_base == 0)
	return 0;

    /* Real Hydra / NE8390 board */
    board->hydra_info.nic_base =
	board->hydra_info.board_base + NE8390_NIC_OFFSET;
    get_ethernet_address((long)board->hydra_info.board_base, paddress);

    for (n = 0, hp = board->hydra; n < HYDRA_MAXDEV; ++n, ++hp)
    {
	hp->q = (queue_t *)NULL;
	hp->sap = 0;
	hp->state = DL_UNBOUND;
    }

    bcopy((caddr_t)paddress, (caddr_t)board->hydra_info.paddress,
	  sizeof(board->hydra_info.paddress));

    return setup_ne2000(board_index, paddress);
}

static int
setup_ne2000(board_index, ethernet_address)
int board_index;
unsigned char ethernet_address[6];
{
    register int i;
    hydra_board_t *board = &hydra_board[board_index];
    hydra_info_t *info = &board->hydra_info;
    volatile unsigned char *nic = info->nic_base;

    bcopy((caddr_t)ethernet_address, (caddr_t)info->paddress,
	  sizeof(info->paddress));

    info->tx_start_page = NE8390_START_PG;
    info->rx_start_page = NE8390_RX_START_PG;
    info->stop_page = NE8390_STOP_PG;
    info->next_pkt = NE8390_RX_START_PG + 1;

    hydra_reset(board_index);

    hydra_outb(nic, NE_CR, NE_CR_P0 | NE_CR_STP | NE_CR_RDMA_ABORT);
    DELAY(10);
    NIC_DELAY();

    hydra_outb(nic, NE_DCR, NE_DCR_WTS | NE_DCR_BOS | NE_DCR_LS | NE_DCR_FT0);
    NIC_DELAY();

    hydra_outb(nic, NE_RBCNT0, 0);
    hydra_outb(nic, NE_RBCNT1, 0);
    NIC_DELAY();

    hydra_outb(nic, NE_PSTART, info->rx_start_page);
    hydra_outb(nic, NE_PSTOP, info->stop_page);
    hydra_outb(nic, NE_BNRY, info->rx_start_page);
    NIC_DELAY();

    hydra_outb(nic, NE_ISR, 0xff);
    NIC_DELAY();

    hydra_outb(nic, NE_CR, NE_CR_P1 | NE_CR_STP | NE_CR_RDMA_ABORT);
    NIC_DELAY();
    for (i = 0; i < 6; i++)
    {
	hydra_outb(nic, NE_PAR0 + i, ethernet_address[i]);
	NIC_DELAY();
    }
    for (i = 0; i < 8; i++)
    {
	hydra_outb(nic, NE_MAR0 + i, 0);
	NIC_DELAY();
    }
    hydra_outb(nic, NE_CURR, info->rx_start_page + 1);
    NIC_DELAY();

    hydra_outb(nic, NE_CR, NE_CR_P0 | NE_CR_STA | NE_CR_RDMA_ABORT);
    NIC_DELAY();

    hydra_outb(nic, NE_TCR, NE_TCR_NORMAL);
    NIC_DELAY();

    hydra_outb(nic, NE_RCR, NE_RCR_AB | NE_RCR_AM);
    NIC_DELAY();

    hydra_outb(nic, NE_IMR, NE_IMR_PRXE | NE_IMR_PTXE | NE_IMR_RXEE | NE_IMR_TXEE | NE_IMR_OVWE | NE_IMR_CNTE);
    NIC_DELAY();

    board->hydra_status.board_state = HYDRA_BOARD_RUNNING;
    board->hydra_status.packets_sent = 0;
    board->hydra_status.packets_received = 0;
    board->hydra_status.allocbs_failed = 0;
    board->hydra_status.couldnt_put = 0;
    board->hydra_status.tx_errors = 0;
    board->hydra_status.rx_errors = 0;
    board->hydra_status.collisions = 0;
    board->hydra_status.overflow = 0;
    board->hydra_status.crc_errors = 0;
    board->hydra_status.framing_errors = 0;
    board->hydra_status.missed_packets = 0;
    board->hydra_status.trailers = 0;
    board->hydra_status.buffer_error = 0;
    board->hydra_status.late_collisions = 0;
    board->hydra_status.loss_of_carrier = 0;
    board->hydra_status.retry_errors = 0;
    board->hydra_status.rx_delivered = 0;
    board->hydra_status.rx_no_sap = 0;
    board->hydra_status.rx_not_for_us = 0;
    board->hydra_status.last_rx_sap = 0;
    board->hydra_status.last_bound_sap = 0;
    board->hydra_status.tx_arp = 0;
    board->hydra_status.tx_ip = 0;
    board->hydra_status.rx_arp_delivered = 0;
    board->hydra_status.rx_ip_delivered = 0;
    board->hydra_status.last_tx_sap = 0;
    board->hydra_status.rx_arp_seen = 0;
    board->hydra_status.rx_ip_seen = 0;
    board->hydra_status.sw_next_pkt = info->next_pkt;
    board->hydra_status.last_ring_page = 0;
    board->hydra_status.last_ring_next = 0;
    board->hydra_status.last_ring_rsr = 0;
    board->hydra_status.last_ring_size = 0;
    board->hydra_status.last_curr = 0;
    board->hydra_status.last_bnry = 0;
    board->hydra_status.rx_bad_next = 0;
    board->hydra_status.rx_bad_rsr = 0;
    board->hydra_status.rx_bad_size = 0;

    return 1;
}

static void
hydra_reset(board_index)
int board_index;
{
    hydra_board_t *board = &hydra_board[board_index];
    volatile unsigned char *nic = board->hydra_info.nic_base;
    int i;

    hydra_outb(nic, NE_CR, NE_CR_P0 | NE_CR_STP | NE_CR_RDMA_ABORT);
    for (i = 0; i < 100; i++)
    {
	DELAY(1);
	if (hydra_inb(nic, NE_ISR) & NE_ISR_RST)
	    break;
    }

    hydra_outb(nic, NE_ISR, 0xff);

    for (i = 0; i < 100; i++)
    {
	DELAY(1);
	if (!(hydra_inb(nic, NE_ISR) & NE_ISR_RST))
	    break;
    }

    DELAY(100);
}

void
hydrainit()
{
    /* Probe for Hydra Systems AmigaNet boards */
    hydraautoconfig();
}

