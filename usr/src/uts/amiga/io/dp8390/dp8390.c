/*
** dp8390.c -- the shared DP8390 (NE2000 family) STREAMS/DLPI core for AMIX:
** DLPI Style 1 connectionless provider, receive ring, transmit, overrun
** recovery, counters and the diagnostic ioctls.  #included by a board
** driver after its backend (see dp8390.h); every symbol here is static.
**
** From the Hydra AmigaNet driver (hydra.c, MIT, isoriano1968), which was
** verified on a real Hydra; the fixes made when it became the core:
**   - CNT: all three tally counters are read (reading clears them), so the
**     counter interrupt ends; missed packets come from CNTR2, CRC errors
**     from CNTR1, frame alignment errors from CNTR0.  Before, only CNTR0 was
**     read and a CNTR1/CNTR2 past 128 kept CNT set: a level-2 storm.
**   - trailer frames (ethertype 0x1001..0x1010) are dropped: their length
**     field pointed up to 8 KB into a 1518-byte buffer.
**   - receive ring overrun (OVW): the DP8390 datasheet's recovery procedure
**     (stop, wait, clear RBCR, loopback, start, drain, ack OVW, normal mode,
**     resend an interrupted transmit).
**   - a bad ring header (next page outside the ring, impossible length, next
**     page that does not follow from the length) resynchronises to the
**     NIC's CURR, with BNRY one behind it, as Linux's lib8390 does; before,
**     BNRY was reset to the ring start while CURR stayed where it was.
**   - a write service procedure: messages queued while the transmitter was
**     busy are sent when it is free again, after PTX and after TXE.
**   - DLPI acknowledgements reuse the request's block when it is big enough
**     and otherwise wait for a buffer (bufcall) instead of sending nothing.
**   - ioctls check b_cont and its size before reading it, and the register
**     and RAM tests need privilege (drv_priv).
**   - DL_UNITDATA_REQ checks its destination address and drops frames over
**     1500 bytes instead of truncating them; short frames are padded with
**     zeros, not with the previous frame.
*/

#define DP_BOARD_RESET		0
#define DP_BOARD_RUNNING	1

#define DP_CLEAR_STATUS		(DP_IOC|1)
#define DP_GET_STATUS		(DP_IOC|2)
#define DP_GET_CONFIG		(DP_IOC|3)
#define DP_NUMBER_OF_BOARDS	(DP_IOC|5)
#define DP_IOCTL_TEST		(DP_IOC|20)

#define DP_TEST_READ_REG	0
#define DP_TEST_WRITE_REG	1
#define DP_TEST_READ_RAM	2
#define DP_TEST_WRITE_RAM	3
#define DP_TEST_GET_STATE	4
#define DP_TEST_TX		5
#define DP_TEST_GET_BOARD	6

#define DP_RAW_DATA		1

extern struct ifstats *ifstats;
extern char *panicstr;
extern int autocon();

static int dp_open(), dp_close(), dp_wput(), dp_wsrv();
static int dp_proto(), dp_setup();
static void dp_ioctl(), dp_rx(), dp_toss(), dp_txdone(), dp_overrun();
static void dp_autoconfig(), dp_intr();

static struct module_info dp_minfo =
{
    DP_MODID, DP_NAME, DP_MINPSZ, DP_MAXPSZ, DP_HIWAT, DP_LOWAT,
};

static struct qinit dp_rinit =
{
    NULL, NULL, dp_open, dp_close, NULL, &dp_minfo, NULL,
};

static struct qinit dp_winit =
{
    dp_wput, dp_wsrv, NULL, NULL, NULL, &dp_minfo, NULL,
};

static dp_board_t dp_board[DP_MAXBOARDS];
static struct
{
    long	address;
    long	size;
} dp_found[DP_MAXBOARDS];
static int dp_nboards;

static unsigned char dp_txbuf[ETH_MAXPACKET + 2];
static unsigned char dp_rxbuf[DP_BUFLEN + 4];

/* the page before p in the receive ring: BNRY trails the next packet */
static int
dp_prev_page(p)
int p;
{
    return (p <= DP_RX_START) ? DP_STOP - 1 : p - 1;
}

/* ---------------------------------------------------------------- open */

static int
dp_initialize(board_index)
int board_index;
{
    dp_board_t *board = &dp_board[board_index];
    int n;

    if (board_index >= dp_nboards || dp_found[board_index].address == 0)
	return ENXIO;
    board->info.board_phys = dp_found[board_index].address;
    board->info.board_size = dp_found[board_index].size;
    if ((n = dp_be_map(board)) != 0)
	return n;

    for (n = 0; n < DP_MAXDEV; n++)
    {
	board->dp[n].q = (queue_t *)NULL;
	board->dp[n].sap = 0;
	board->dp[n].state = DL_UNBOUND;
	board->dp[n].bufcall_id = 0;
    }

    dp_be_reset(board);
    dp_be_start(board);
    dp_be_get_mac(board, board->info.paddress);
    bzero((caddr_t)&board->status, sizeof(board->status));
    return dp_setup(board) ? 0 : EIO;
}

static int
dp_open(q, devp, flag, sflag, credp)
register queue_t *q;
dev_t *devp;
int flag, sflag;
cred_t *credp;
{
    register dp_t *hp;
    register int minor_device, board_index, sap_index;
    dp_board_t *board;
    int e;

    if (sflag == MODOPEN || sflag == CLONEOPEN)
	return EINVAL;

    dp_autoconfig();

    minor_device = getminor(*devp);
    board_index = minor_device & 0x0F;
    sap_index = (minor_device & 0xF0) >> 4;
    if (board_index >= DP_MAXBOARDS)
	return ENODEV;
    board = &dp_board[board_index];

    if (board->status.board_state != DP_BOARD_RUNNING)
    {
	if ((e = dp_initialize(board_index)) != 0)
	    return e;
    }

    if (sap_index == 0)
    {
	for (sap_index = 0, hp = board->dp; sap_index < DP_MAXDEV; sap_index++, hp++)
	    if (!hp->q)
		break;
    }
    else
	--sap_index;

    if (sap_index >= DP_MAXDEV)
	return ENOSPC;

    hp = &board->dp[sap_index];
    if (hp->q)
	return EBUSY;

    *devp = makedevice(getmajor(*devp), (((sap_index + 1) << 4) | board_index));

    q->q_ptr = (char *)hp;
    WR(q)->q_ptr = (char *)hp;
    hp->q = q;
    hp->board_index = board_index;
    hp->flags = 0;
    hp->state = DL_UNATTACHED;
    hp->sap = 0;
    hp->bufcall_id = 0;

    if (!board->ifstats.ifs_name)
    {
	board->ifstats.ifs_name = DP_NAME;
	board->ifstats.ifs_mtu = DP_MTU;
	board->ifstats.ifs_next = ifstats;
	ifstats = &board->ifstats;
    }
    board->ifstats.ifs_unit = board_index;
    board->ifstats.ifs_active = 1;
    board->if_flags = IFF_BROADCAST | IFF_NOTRAILERS | IFF_RUNNING;

    return 0;
}

static int
dp_close(q)
register queue_t *q;
{
    register dp_t *hp = (dp_t *)q->q_ptr;
    register dp_board_t *board = &dp_board[hp->board_index];
    int dev, s;

    s = spldp();
    if (hp->bufcall_id)
    {
	unbufcall(hp->bufcall_id);
	hp->bufcall_id = 0;
    }
    flushq(WR(q), FLUSHALL);
    hp->q = 0;
    hp->sap = 0;
    OTHERQ(q)->q_ptr = NULL;
    q->q_ptr = NULL;

    for (dev = 0, hp = board->dp; dev < DP_MAXDEV; ++dev, ++hp)
	if (hp->q)
	    break;

    if (dev >= DP_MAXDEV)
    {
	/* the last stream: quiet the NIC, so it raises no interrupt */
	dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STP | NE_CR_RDMA_ABORT);
	dp_outb(board, NE_IMR, 0);
	dp_outb(board, NE_ISR, 0xff);
	dp_be_stop(board);

	board->status.board_state = DP_BOARD_RESET;
	board->ifstats.ifs_unit = 0;
	board->ifstats.ifs_active = 0;
	board->ifstats.ifs_ipackets = 0;
	board->ifstats.ifs_ierrors = 0;
	board->ifstats.ifs_opackets = 0;
	board->ifstats.ifs_oerrors = 0;
    }
    splx(s);
    return 0;
}

/* ---------------------------------------------------------------- write */

static int
dp_wput(q, mp)
register queue_t *q;
register mblk_t *mp;
{
    register int s;

    s = spldp();
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
	/* keep the order: behind what is queued already */
	if (q->q_first || !dp_proto(q, mp))
	    putq(q, mp);
	break;

    case M_IOCDATA:
    case M_IOCTL:
	dp_ioctl(q, mp);
	break;

    default:
	freemsg(mp);
	break;
    }
    splx(s);
    return 0;
}

/* the messages dp_proto could not handle at once: transmitter busy, or no
 * buffer for an acknowledgement (then a bufcall enables the queue again) */
static int
dp_wsrv(q)
queue_t *q;
{
    mblk_t *mp;
    int s;

    s = spldp();
    while ((mp = getq(q)) != NULL)
    {
	if (!dp_proto(q, mp))
	{
	    putbq(q, mp);
	    break;
	}
    }
    splx(s);
    return 0;
}

/* bufcall() callback: a buffer may be there now, run the write queue */
static void
dp_bufcb(arg)
long arg;
{
    queue_t *q = (queue_t *)arg;
    dp_t *hp = (dp_t *)q->q_ptr;

    if (hp)
	hp->bufcall_id = 0;
    qenable(q);
}

/* a reply block of size bytes for the request mp: mp itself when its data
 * block is big enough and not shared, else a new one (mp freed); NULL when
 * there is no memory -- mp is then untouched and a bufcall will enable q */
static mblk_t *
dp_replyblk(q, mp, size)
queue_t *q;
mblk_t *mp;
int size;
{
    dp_t *hp = (dp_t *)q->q_ptr;
    mblk_t *bp;

    if (mp->b_datap->db_ref == 1 && mp->b_datap->db_lim - mp->b_datap->db_base >= size)
    {
	if (mp->b_cont)
	{
	    freemsg(mp->b_cont);
	    mp->b_cont = NULL;
	}
	mp->b_rptr = mp->b_wptr = mp->b_datap->db_base;
	return mp;
    }
    if ((bp = allocb(size, BPRI_HI)) != NULL)
    {
	freemsg(mp);
	return bp;
    }
    if (!hp->bufcall_id)
	hp->bufcall_id = bufcall(size, BPRI_HI, dp_bufcb, (long)q);
    return NULL;
}

static int
dp_okack(q, mp, prim)
queue_t *q;
mblk_t *mp;
long prim;
{
    dl_ok_ack_t *okp;

    if (!(mp = dp_replyblk(q, mp, sizeof(dl_ok_ack_t))))
	return 0;
    mp->b_datap->db_type = M_PCPROTO;
    okp = (dl_ok_ack_t *)mp->b_wptr;
    okp->dl_primitive = DL_OK_ACK;
    okp->dl_correct_primitive = prim;
    mp->b_wptr += sizeof(dl_ok_ack_t);
    qreply(q, mp);
    return 1;
}

/* DL_UNITDATA_REQ: 1 = done with mp (sent or dropped), 0 = transmitter busy */
static int
dp_xmit(q, mp)
queue_t *q;
mblk_t *mp;
{
    register dp_t *hp = (dp_t *)q->q_ptr;
    dp_board_t *board = &dp_board[hp->board_index];
    dl_unitdata_req_t *dp = (dl_unitdata_req_t *)mp->b_rptr;
    register int pktsize, copy;
    mblk_t *bp;

    if (board->status.board_state != DP_BOARD_RUNNING)
    {
	freemsg(mp);
	return 1;
    }
    if (blklen(mp) < sizeof(dl_unitdata_req_t) ||
	dp->dl_dest_addr_length < sizeof(ether_addr_t) ||
	dp->dl_dest_addr_offset < sizeof(dl_unitdata_req_t) ||
	dp->dl_dest_addr_offset + sizeof(ether_addr_t) > blklen(mp))
    {
	board->status.tx_errors++;
	board->ifstats.ifs_oerrors++;
	freemsg(mp);
	return 1;
    }

    dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STA | NE_CR_RDMA_ABORT);
    if (dp_inb(board, NE_CR) & NE_CR_TXP)
	return 0;

    bcopy((caddr_t)mp->b_rptr + dp->dl_dest_addr_offset, (caddr_t)&dp_txbuf[0],
	  sizeof(ether_addr_t));
    bcopy((caddr_t)board->info.paddress, (caddr_t)&dp_txbuf[6], sizeof(ether_addr_t));
    bcopy((caddr_t)&hp->sap, (caddr_t)&dp_txbuf[12], sizeof(hp->sap));
    board->status.last_tx_sap = hp->sap;

    pktsize = ETH_HEADER_SIZE;
    for (bp = mp->b_cont; bp; bp = bp->b_cont)
    {
	copy = blklen(bp);
	if (copy <= 0)
	    continue;
	if (pktsize + copy > ETH_MAXPACKET)
	{
	    board->status.tx_errors++;
	    board->ifstats.ifs_oerrors++;
	    freemsg(mp);
	    return 1;
	}
	bcopy((caddr_t)bp->b_rptr, (caddr_t)&dp_txbuf[pktsize], copy);
	pktsize += copy;
    }
    freemsg(mp);

    if (pktsize < ETH_MINFRAME)
    {
	bzero((caddr_t)&dp_txbuf[pktsize], ETH_MINFRAME - pktsize);
	pktsize = ETH_MINFRAME;
    }

    if (!dp_be_ram_write(board, dp_txbuf, pktsize, DP_TX_START << 8))
    {
	/* the buffer RAM did not take the frame (remote DMA): start over */
	board->status.tx_errors++;
	board->status.buffer_error++;
	board->ifstats.ifs_oerrors++;
	dp_be_reset(board);
	dp_be_start(board);
	dp_setup(board);
	return 1;
    }

    dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STA | NE_CR_RDMA_ABORT);
    dp_outb(board, NE_TPSR, DP_TX_START);
    dp_outb(board, NE_TBCNT0, pktsize & 0xff);
    dp_outb(board, NE_TBCNT1, (pktsize >> 8) & 0xff);
    dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_TXP | NE_CR_STA | NE_CR_RDMA_ABORT);

    board->status.packets_sent++;
    if (hp->sap == ETHERTYPE_ARP)
	board->status.tx_arp++;
    if (hp->sap == ETHERTYPE_IP)
	board->status.tx_ip++;
    board->ifstats.ifs_opackets++;
    return 1;
}

/* 1 = done with mp, 0 = put it back on the queue (busy or no buffer) */
static int
dp_proto(q, mp)
queue_t *q;
mblk_t *mp;
{
    register union DL_primitives *p = (union DL_primitives *)mp->b_rptr;
    register dp_t *hp = (dp_t *)q->q_ptr;
    register dp_board_t *board = &dp_board[hp->board_index];
    long prim;

    if (blklen(mp) < sizeof(long))
    {
	freemsg(mp);
	return 1;
    }
    prim = p->dl_primitive;

    switch (prim)
    {
    case DL_UNITDATA_REQ:
	return dp_xmit(q, mp);

    case DL_ATTACH_REQ:
	if (!dp_okack(q, mp, prim))
	    return 0;
	hp->state = DL_UNBOUND;
	return 1;

    case DL_BIND_REQ:
    {
	dl_bind_ack_t *ackp;
	unsigned short sap;

	if (blklen(mp) < sizeof(dl_bind_req_t))
	{
	    freemsg(mp);
	    return 1;
	}
	sap = ((dl_bind_req_t *)mp->b_rptr)->dl_sap;
	if (!(mp = dp_replyblk(q, mp, sizeof(*ackp) + sizeof(ether_addr_t))))
	    return 0;
	hp->sap = sap;
	board->status.last_bound_sap = sap;
	hp->state = DL_IDLE;

	mp->b_datap->db_type = M_PCPROTO;
	ackp = (dl_bind_ack_t *)mp->b_wptr;
	ackp->dl_primitive = DL_BIND_ACK;
	ackp->dl_sap = sap;
	ackp->dl_addr_length = sizeof(ether_addr_t);
	ackp->dl_addr_offset = sizeof(*ackp);
	ackp->dl_max_conind = 0;
	ackp->dl_growth = 0;
	mp->b_wptr += sizeof(*ackp);
	bcopy((caddr_t)board->info.paddress, (caddr_t)mp->b_wptr, sizeof(ether_addr_t));
	mp->b_wptr += sizeof(ether_addr_t);
	qreply(q, mp);
	return 1;
    }

    case DL_INFO_REQ:
    {
	dl_info_ack_t *ackp;

	if (!(mp = dp_replyblk(q, mp, sizeof(dl_info_ack_t) + sizeof(ether_addr_t))))
	    return 0;
	mp->b_datap->db_type = M_PCPROTO;
	ackp = (dl_info_ack_t *)mp->b_wptr;
	ackp->dl_primitive = DL_INFO_ACK;
	ackp->dl_max_sdu = ETH_MAXDATA;
	ackp->dl_min_sdu = 1;
	ackp->dl_addr_length = sizeof(ether_addr_t);
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
	mp->b_wptr += sizeof(dl_info_ack_t);
	if (hp->state == DL_IDLE)
	{
	    ackp->dl_addr_offset = sizeof(dl_info_ack_t);
	    bcopy((caddr_t)board->info.paddress, (caddr_t)mp->b_wptr, sizeof(ether_addr_t));
	    mp->b_wptr += sizeof(ether_addr_t);
	}
	else
	    ackp->dl_addr_offset = 0;
	qreply(q, mp);
	return 1;
    }

    case DL_UNBIND_REQ:
	if (!dp_okack(q, mp, prim))
	    return 0;
	hp->state = DL_UNBOUND;
	hp->sap = 0;
	return 1;

    case DL_DETACH_REQ:
	if (!dp_okack(q, mp, prim))
	    return 0;
	hp->state = DL_UNATTACHED;
	return 1;

    default:
	freemsg(mp);
	return 1;
    }
}

/* ---------------------------------------------------------------- ioctl */

static void
dp_iocack(q, mp, count, error)
queue_t *q;
mblk_t *mp;
int count, error;
{
    struct iocblk *iocbp = (struct iocblk *)mp->b_rptr;

    mp->b_datap->db_type = error ? M_IOCNAK : M_IOCACK;
    if (error || count == 0)
    {
	if (mp->b_cont)
	{
	    freemsg(mp->b_cont);
	    mp->b_cont = NULL;
	}
	count = 0;
    }
    iocbp->ioc_count = count;
    iocbp->ioc_rval = 0;
    iocbp->ioc_error = error;
    putnext(RD(q), mp);
}

/* reply with size bytes of data: a M_COPYOUT to arg (TRANSPARENT) or an
 * M_IOCACK carrying them (I_STR) */
static void
dp_iocreply(q, mp, data, size, transparent, arg)
queue_t *q;
mblk_t *mp;
caddr_t data;
int size, transparent;
caddr_t arg;
{
    mblk_t *bp;

    if (!(bp = allocb(size, BPRI_MED)))
    {
	dp_iocack(q, mp, 0, ENOMEM);
	return;
    }
    bcopy(data, (caddr_t)bp->b_wptr, size);
    bp->b_wptr += size;
    if (mp->b_cont)
	freemsg(mp->b_cont);
    mp->b_cont = bp;

    if (transparent)
    {
	struct copyreq *creq = (struct copyreq *)mp->b_rptr;

	mp->b_datap->db_type = M_COPYOUT;
	mp->b_wptr = mp->b_rptr + sizeof(struct copyreq);
	creq->cq_addr = arg;
	creq->cq_size = size;
	creq->cq_flag = 0;
	creq->cq_private = (mblk_t *)0;
	putnext(RD(q), mp);
    }
    else
	dp_iocack(q, mp, size, 0);
}

static int
dp_test_priv(op)
int op;
{
    return op == DP_TEST_READ_REG || op == DP_TEST_WRITE_REG ||
	   op == DP_TEST_READ_RAM || op == DP_TEST_WRITE_RAM || op == DP_TEST_TX;
}

static int
dp_test_ram_ok(t)
dp_test_t *t;
{
    return t->len >= 0 && t->len <= sizeof(t->data) &&
	   t->page >= DP_RAM_START && t->page < DP_STOP &&
	   (t->page << 8) + t->len <= (DP_STOP << 8);
}

static void
dp_test(board, t)
dp_board_t *board;
dp_test_t *t;
{
    int s = spldp();

    switch (t->op)
    {
    case DP_TEST_READ_REG:
	t->val = (t->reg <= 0x0f && dp_be_test_reg_ok(board, t->reg, 0)) ? dp_inb(board, t->reg) : 0;
	break;

    case DP_TEST_WRITE_REG:
	if (t->reg <= 0x0f && dp_be_test_reg_ok(board, t->reg, 1))
	    dp_outb(board, t->reg, t->val);
	break;

    case DP_TEST_READ_RAM:
	if (dp_test_ram_ok(t))
	    dp_be_ram_read(board, t->data, t->len, t->page << 8);
	break;

    case DP_TEST_WRITE_RAM:
	if (dp_test_ram_ok(t))
	    dp_be_ram_write(board, t->data, t->len, t->page << 8);
	break;

    case DP_TEST_GET_STATE:
	/* no live register snapshot: on the Hydra a read around register 6
	 * (FIFO/TBCR1) can bus trap, and a diagnostic must not panic */
	t->tsr = t->isr = t->rsr = t->ncr = 0;
	t->tcr = t->rcr = t->curr = t->bnry = 0;
	break;

    case DP_TEST_GET_BOARD:
	t->page = (int)board->info.board_phys;
	bcopy((caddr_t)board->info.paddress, (caddr_t)t->data, 6);
	t->data[6] = board->status.board_state;
	break;
    }
    splx(s);
}

static void
dp_ioctl(q, mp)
queue_t *q;
mblk_t *mp;
{
    register struct iocblk *iocbp = (struct iocblk *)mp->b_rptr;
    register dp_t *hp = (dp_t *)q->q_ptr;
    register dp_board_t *board = &dp_board[hp->board_index];
    int transparent;
    caddr_t arg = 0;

    if (mp->b_datap->db_type == M_IOCDATA)
    {
	struct copyresp *cp = (struct copyresp *)mp->b_rptr;
	dp_test_t *t;

	if (cp->cp_rval)
	{
	    freemsg(mp);		/* the copy failed; the head has the error */
	    return;
	}
	if (!cp->cp_private)
	{
	    dp_iocack(q, mp, 0, 0);	/* our M_COPYOUT is done */
	    return;
	}
	/* the M_COPYIN of DP_IOCTL_TEST is done: cp_private is the user address */
	if (cp->cp_cmd != DP_IOCTL_TEST || !mp->b_cont ||
	    blklen(mp->b_cont) < sizeof(dp_test_t))
	{
	    dp_iocack(q, mp, 0, EINVAL);
	    return;
	}
	t = (dp_test_t *)mp->b_cont->b_rptr;
	if (dp_test_priv(t->op) && drv_priv(cp->cp_cr))
	{
	    dp_iocack(q, mp, 0, EPERM);
	    return;
	}
	dp_test(board, t);
	arg = (caddr_t)cp->cp_private;
	{
	    struct copyreq *creq = (struct copyreq *)mp->b_rptr;

	    mp->b_datap->db_type = M_COPYOUT;
	    mp->b_wptr = mp->b_rptr + sizeof(struct copyreq);
	    creq->cq_addr = arg;
	    creq->cq_size = sizeof(dp_test_t);
	    creq->cq_flag = 0;
	    creq->cq_private = (mblk_t *)0;
	    mp->b_cont->b_wptr = mp->b_cont->b_rptr + sizeof(dp_test_t);
	    putnext(RD(q), mp);
	}
	return;
    }

    if (blklen(mp) < sizeof(struct iocblk))
    {
	freemsg(mp);
	return;
    }
    transparent = (iocbp->ioc_count == TRANSPARENT);
    if (transparent)
    {
	if (!mp->b_cont || blklen(mp->b_cont) < sizeof(caddr_t))
	{
	    dp_iocack(q, mp, 0, EINVAL);
	    return;
	}
	arg = *(caddr_t *)mp->b_cont->b_rptr;
    }

    switch (iocbp->ioc_cmd)
    {
    case SIOCSIFFLAGS:
    case SIOCGIFFLAGS:
	mp->b_datap->db_type = M_IOCACK;
	putnext(RD(q), mp);
	return;

    case DP_NUMBER_OF_BOARDS:
	dp_iocreply(q, mp, (caddr_t)&dp_nboards, sizeof(int), transparent, arg);
	return;

    case DP_GET_CONFIG:
    {
	dp_config_t cfg;

	bzero((caddr_t)&cfg, sizeof(cfg));
	cfg.board_base = (long)board->info.board_phys;
	cfg.board_debug = board->status.board_debug;
	bcopy((caddr_t)board->info.paddress, (caddr_t)cfg.paddress, 6);
	dp_iocreply(q, mp, (caddr_t)&cfg, sizeof(cfg), transparent, arg);
	return;
    }

    case DP_GET_STATUS:
    {
	dp_status_t status;
	int i;

	status = board->status;
	status.open_streams = status.bound_streams = 0;
	status.arp_streams = status.ip_streams = 0;
	for (i = 0; i < DP_MAXDEV; i++)
	{
	    if (!board->dp[i].q)
		continue;
	    status.open_streams++;
	    if (board->dp[i].sap)
		status.bound_streams++;
	    if (board->dp[i].sap == ETHERTYPE_ARP)
		status.arp_streams++;
	    if (board->dp[i].sap == ETHERTYPE_IP)
		status.ip_streams++;
	}
	status.if_active = board->ifstats.ifs_active;
	status.if_ipackets = board->ifstats.ifs_ipackets;
	status.if_opackets = board->ifstats.ifs_opackets;
	status.if_ierrors = board->ifstats.ifs_ierrors;
	status.if_oerrors = board->ifstats.ifs_oerrors;
	status.sw_next_pkt = board->info.next_pkt;
	dp_iocreply(q, mp, (caddr_t)&status, sizeof(status), transparent, arg);
	return;
    }

    case DP_IOCTL_TEST:
    {
	dp_test_t *t;

	if (transparent)
	{
	    struct copyreq *creq = (struct copyreq *)mp->b_rptr;

	    mp->b_datap->db_type = M_COPYIN;
	    mp->b_wptr = mp->b_rptr + sizeof(struct copyreq);
	    creq->cq_addr = arg;
	    creq->cq_size = sizeof(dp_test_t);
	    creq->cq_flag = 0;
	    creq->cq_private = (mblk_t *)arg;
	    freemsg(mp->b_cont);
	    mp->b_cont = NULL;
	    putnext(RD(q), mp);
	    return;
	}
	if (!mp->b_cont || iocbp->ioc_count < sizeof(dp_test_t) ||
	    blklen(mp->b_cont) < sizeof(dp_test_t))
	{
	    dp_iocack(q, mp, 0, EINVAL);
	    return;
	}
	t = (dp_test_t *)mp->b_cont->b_rptr;
	if (dp_test_priv(t->op) && drv_priv(iocbp->ioc_cr))
	{
	    dp_iocack(q, mp, 0, EPERM);
	    return;
	}
	dp_test(board, t);
	dp_iocack(q, mp, sizeof(dp_test_t), 0);
	return;
    }

    default:
	dp_iocack(q, mp, 0, EINVAL);
	return;
    }
}

/* ---------------------------------------------------------------- receive */

/* skip whatever is in the ring: continue at the NIC's CURR, BNRY behind it */
static void
dp_resync(board)
dp_board_t *board;
{
    int curr;

    dp_outb(board, NE_CR, NE_CR_P1 | NE_CR_STA | NE_CR_RDMA_ABORT);
    curr = dp_inb(board, NE_CURR);
    dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STA | NE_CR_RDMA_ABORT);
    if (curr < DP_RX_START || curr >= DP_STOP)
	curr = DP_RX_START;	/* cannot be: the NIC keeps CURR in the ring */
    board->info.next_pkt = curr;
    dp_outb(board, NE_BNRY, dp_prev_page(curr));
}

static void
dp_rx(board)
dp_board_t *board;
{
    dp_info_t *info = &board->info;
    int curr, this, next, pktsize, count, pages, loop;
    unsigned char hdr[4], rsr;

    for (loop = 0; loop < 256; loop++)
    {
	dp_outb(board, NE_CR, NE_CR_P1 | NE_CR_STA | NE_CR_RDMA_ABORT);
	curr = dp_inb(board, NE_CURR);
	dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STA | NE_CR_RDMA_ABORT);
	board->status.last_curr = curr;
	board->status.last_bnry = dp_inb(board, NE_BNRY);

	this = info->next_pkt;
	if (this == curr)
	    return;

	if (!dp_be_ram_read(board, hdr, 4, this << 8))
	{
	    board->status.rx_errors++;
	    board->ifstats.ifs_ierrors++;
	    dp_resync(board);
	    return;
	}
	next = hdr[0];
	rsr = hdr[1];
	count = ((unsigned int)hdr[3] << 8) | hdr[2];
	board->status.last_ring_page = this;
	board->status.last_ring_next = next;
	board->status.last_ring_rsr = rsr;
	board->status.last_ring_size = count;

	/* where the next frame must start: count bytes (header included, or
	 * not -- either way) from the start of this page, plus at most one */
	pages = next - this;
	if (pages <= 0)
	    pages += DP_STOP - DP_RX_START;
	pktsize = count - 4;		/* without the 4-byte CRC */
	if (next < DP_RX_START || next >= DP_STOP)
	{
	    board->status.rx_errors++;
	    board->status.buffer_error++;
	    board->status.rx_bad_next++;
	    board->ifstats.ifs_ierrors++;
	    dp_resync(board);
	    return;
	}
	if (pktsize < ETH_MINFRAME || pktsize > DP_BUFLEN ||
	    pages < ((count + 255) >> 8) || pages > (count >> 8) + 2)
	{
	    board->status.rx_errors++;
	    board->status.buffer_error++;
	    board->status.rx_bad_size++;
	    board->ifstats.ifs_ierrors++;
	    dp_resync(board);
	    return;
	}

	if (!(rsr & NE_RSR_PRX))
	{
	    board->status.rx_errors++;
	    board->status.rx_bad_rsr++;
	    board->ifstats.ifs_ierrors++;
	}
	else
	{
	    int offset = (this << 8) + 4;
	    int stop = DP_STOP << 8;
	    int ok;

	    if (offset + pktsize > stop)
	    {
		int semi = stop - offset;

		ok = dp_be_ram_read(board, dp_rxbuf, semi, offset) &&
		     dp_be_ram_read(board, dp_rxbuf + semi, pktsize - semi, DP_RX_START << 8);
	    }
	    else
		ok = dp_be_ram_read(board, dp_rxbuf, pktsize, offset);
	    if (!ok)
	    {
		board->status.rx_errors++;
		board->ifstats.ifs_ierrors++;
		dp_resync(board);
		return;
	    }
	    board->status.packets_received++;
	    board->ifstats.ifs_ipackets++;
	    if (rsr & NE_RSR_FO)
		board->status.overflow++;
	}

	info->next_pkt = next;
	dp_outb(board, NE_BNRY, dp_prev_page(next));

	if (rsr & NE_RSR_PRX)
	    dp_toss(board, pktsize);
    }
    board->status.rx_errors++;		/* 256 frames in one go: let it rest */
}

static void
dp_toss(board, count)
dp_board_t *board;
int count;
{
    int j, length, sap_matched = 0;
    unsigned short sap;
    dp_t *hp = board->dp;
    static unsigned char broadcast_address[6] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };
    ether_header_t *packet_header = (ether_header_t *)dp_rxbuf;

    sap = packet_header->ether_type;
    board->status.last_rx_sap = sap;
    if (sap == ETHERTYPE_ARP)
	board->status.rx_arp_seen++;
    if (sap == ETHERTYPE_IP)
	board->status.rx_ip_seen++;

    /* trailer encapsulation (we never ask for it: IFF_NOTRAILERS): dropped */
    if (sap >= ETHERTYPE_TRAIL && sap <= ETHERTYPE_TRAIL + ETHERTYPE_NTRAILER)
    {
	board->status.trailers++;
	return;
    }
    length = count - ETH_HEADER_SIZE;

    for (j = 0; j < DP_MAXDEV; j++, hp++)
    {
	mblk_t *mp = (mblk_t *)NULL;

	if (!hp->q)
	    continue;

	if (hp->flags & DP_RAW_DATA)
	{
	    if (!canput(hp->q->q_next))
	    {
		board->status.couldnt_put++;
		continue;
	    }
	    if ((mp = allocb(sizeof(dp_status_t), BPRI_MED)) != NULL &&
		(mp->b_cont = allocb(count, BPRI_MED)) != NULL)
	    {
		mp->b_datap->db_type = M_PROTO;
		bcopy((caddr_t)&board->status, (caddr_t)mp->b_wptr, sizeof(dp_status_t));
		mp->b_wptr += sizeof(dp_status_t);
		mp->b_cont->b_datap->db_type = M_DATA;
		bcopy((caddr_t)dp_rxbuf, (caddr_t)mp->b_cont->b_wptr, count);
		mp->b_cont->b_wptr += count;
		putnext(hp->q, mp);
	    }
	    else
	    {
		board->status.allocbs_failed++;
		if (mp)
		    freemsg(mp);
	    }
	    continue;
	}
	if (hp->sap != sap)
	    continue;
	sap_matched = 1;

	if (bcmp((char *)packet_header->ether_dhost, (char *)board->info.paddress,
		 sizeof(ether_addr_t)) &&
	    bcmp((char *)packet_header->ether_dhost, (char *)broadcast_address,
		 sizeof(broadcast_address)))
	{
	    board->status.rx_not_for_us++;
	    continue;
	}
	if (!canput(hp->q->q_next))
	{
	    board->status.couldnt_put++;
	    continue;
	}
	if ((mp = allocb(sizeof(union DL_primitives) + 2 * sizeof(ether_addr_t),
			 BPRI_MED)) != NULL &&
	    (mp->b_cont = allocb(length, BPRI_MED)) != NULL)
	{
	    register dl_unitdata_ind_t *dp = (dl_unitdata_ind_t *)mp->b_wptr;

	    dp->dl_primitive = DL_UNITDATA_IND;
	    dp->dl_dest_addr_length = sizeof(ether_addr_t);
	    dp->dl_dest_addr_offset = sizeof(union DL_primitives);
	    dp->dl_src_addr_length = sizeof(ether_addr_t);
	    dp->dl_src_addr_offset = sizeof(union DL_primitives) + sizeof(ether_addr_t);
	    dp->dl_reserved = 0;
	    bcopy((caddr_t)packet_header->ether_dhost,
		  (caddr_t)mp->b_wptr + dp->dl_dest_addr_offset, sizeof(ether_addr_t));
	    bcopy((caddr_t)packet_header->ether_shost,
		  (caddr_t)mp->b_wptr + dp->dl_src_addr_offset, sizeof(ether_addr_t));
	    mp->b_wptr += sizeof(union DL_primitives) + 2 * sizeof(ether_addr_t);
	    mp->b_datap->db_type = M_PROTO;

	    bcopy((caddr_t)&dp_rxbuf[ETH_HEADER_SIZE], (caddr_t)mp->b_cont->b_wptr, length);
	    mp->b_cont->b_wptr += length;
	    mp->b_cont->b_datap->db_type = M_DATA;

	    putnext(hp->q, mp);
	    board->status.rx_delivered++;
	    if (sap == ETHERTYPE_ARP)
		board->status.rx_arp_delivered++;
	    if (sap == ETHERTYPE_IP)
		board->status.rx_ip_delivered++;
	}
	else
	{
	    board->status.allocbs_failed++;
	    if (mp)
		freemsg(mp);
	}
    }
    if (!sap_matched)
	board->status.rx_no_sap++;
}

/* ---------------------------------------------------------------- interrupt */

/* the transmitter is free: let every write queue with work run again */
static void
dp_txdone(board)
dp_board_t *board;
{
    int n;

    for (n = 0; n < DP_MAXDEV; n++)
	if (board->dp[n].q && WR(board->dp[n].q)->q_first)
	    qenable(WR(board->dp[n].q));
}

/*
** Receive ring overflow, the DP8390 datasheet's procedure: note TXP, stop,
** wait for the stop (RST, at least 1.6 ms), clear the remote byte count,
** decide about a resend, loopback mode, start, drain the ring, ack OVW,
** normal mode, resend.
*/
static void
dp_overrun(board)
dp_board_t *board;
{
    int txp, resend = 0, i;

    txp = dp_inb(board, NE_CR) & NE_CR_TXP;
    dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STP | NE_CR_RDMA_ABORT);
    for (i = 0; i < 10000; i++)
	if (dp_inb(board, NE_ISR) & NE_ISR_RST)
	    break;
    DELAY(20);
    dp_outb(board, NE_RBCNT0, 0);
    dp_outb(board, NE_RBCNT1, 0);
    if (txp && !(dp_inb(board, NE_ISR) & (NE_ISR_PTX | NE_ISR_TXE)))
	resend = 1;
    dp_outb(board, NE_TCR, NE_TCR_LB0);
    dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STA | NE_CR_RDMA_ABORT);
    dp_rx(board);
    dp_outb(board, NE_ISR, NE_ISR_OVW);
    dp_outb(board, NE_TCR, NE_TCR_NORMAL);
    if (resend)
	dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_TXP | NE_CR_STA | NE_CR_RDMA_ABORT);
}

static void
dp_intr()
{
    unsigned char status;
    int board_index, loop;

    if (panicstr)
	return;

    for (board_index = 0; board_index < dp_nboards; ++board_index)
    {
	dp_board_t *board = &dp_board[board_index];

	if (board->status.board_state != DP_BOARD_RUNNING || board->info.nic_base == 0)
	    continue;
	if (!dp_be_irq_pending(board))
	    continue;

	for (loop = 0; loop < 8; loop++)
	{
	    dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STA | NE_CR_RDMA_ABORT);
	    status = dp_inb(board, NE_ISR) & NE_ISR_ALL;
	    if (status == 0)
		break;

	    if (status & NE_ISR_OVW)
	    {
		board->status.overflow++;
		board->ifstats.ifs_ierrors++;
		if (status & NE_ISR_RXE)
		    board->status.rx_errors++;
		dp_outb(board, NE_ISR, status & (NE_ISR_PRX | NE_ISR_RXE));
		dp_overrun(board);	/* drains the ring and acks OVW */
	    }
	    else if (status & (NE_ISR_PRX | NE_ISR_RXE))
	    {
		if (status & NE_ISR_RXE)
		{
		    board->status.rx_errors++;
		    board->ifstats.ifs_ierrors++;
		}
		dp_outb(board, NE_ISR, status & (NE_ISR_PRX | NE_ISR_RXE));
		dp_rx(board);
	    }

	    if (status & (NE_ISR_PTX | NE_ISR_TXE))
	    {
		dp_outb(board, NE_ISR, status & (NE_ISR_PTX | NE_ISR_TXE));
		if (status & NE_ISR_TXE)
		{
		    unsigned char tsr = dp_inb(board, NE_TSR);

		    board->status.tx_errors++;
		    board->ifstats.ifs_oerrors++;
		    if (tsr & NE_TSR_COL)
			board->status.collisions++;
		    if (tsr & NE_TSR_ABT)
			board->status.retry_errors++;
		    if (tsr & NE_TSR_OWC)
			board->status.late_collisions++;
		    if (tsr & NE_TSR_CRS)
			board->status.loss_of_carrier++;
		}
		dp_txdone(board);
	    }

	    if (status & NE_ISR_CNT)
	    {
		/* reading a tally counter clears it, and with it CNT */
		board->status.framing_errors += dp_inb(board, NE_CNTR0);
		board->status.crc_errors += dp_inb(board, NE_CNTR1);
		board->status.missed_packets += dp_inb(board, NE_CNTR2);
		dp_outb(board, NE_ISR, NE_ISR_CNT);
	    }
	}
    }
}

/* ---------------------------------------------------------------- setup */

/* AutoConfig only: a machine without the board is normal, and says nothing */
static void
dp_autoconfig()
{
    static int done;
    long addr, size;
    int i;

    if (done)
	return;
    done = 1;
    dp_nboards = 0;
    for (i = 0; i < DP_MAXBOARDS; i++)
    {
	addr = 0;
	size = 0;
	if (!autocon(DP_BOARD_ID, i, &addr, &size))
	    break;
	if (!dp_be_addr_ok(addr, size))
	    continue;
	dp_found[dp_nboards].address = addr;
	dp_found[dp_nboards].size = size;
	dp_nboards++;
    }
}

/* the NIC from stopped to running: the order of the verified Hydra driver */
static int
dp_setup(board)
dp_board_t *board;
{
    register int i;
    dp_info_t *info = &board->info;

    info->next_pkt = DP_RX_START + 1;

    dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STP | NE_CR_RDMA_ABORT);
    dp_outb(board, NE_IMR, 0);
    dp_outb(board, NE_DCR, DP_DCR);
    dp_outb(board, NE_RBCNT0, 0);
    dp_outb(board, NE_RBCNT1, 0);
    dp_outb(board, NE_PSTART, DP_RX_START);
    dp_outb(board, NE_PSTOP, DP_STOP);
    dp_outb(board, NE_BNRY, DP_RX_START);
    dp_outb(board, NE_ISR, 0xff);

    dp_outb(board, NE_CR, NE_CR_P1 | NE_CR_STP | NE_CR_RDMA_ABORT);
    for (i = 0; i < 6; i++)
	dp_outb(board, NE_PAR0 + i, info->paddress[i]);
    for (i = 0; i < 8; i++)
	dp_outb(board, NE_MAR0 + i, 0);
    dp_outb(board, NE_CURR, DP_RX_START + 1);

    dp_outb(board, NE_CR, NE_CR_P0 | NE_CR_STA | NE_CR_RDMA_ABORT);
    dp_outb(board, NE_TCR, NE_TCR_NORMAL);
    dp_outb(board, NE_RCR, DP_RCR);
    dp_outb(board, NE_IMR, NE_ISR_ALL);

    board->status.sw_next_pkt = info->next_pkt;
    board->status.board_state = DP_BOARD_RUNNING;
    return 1;
}
