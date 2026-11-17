/*
** dp8390.h -- the shared DP8390 (NE2000 family) STREAMS/DLPI core for AMIX.
**
** The core (dp8390.c) is not an object of its own: a board driver (hydra.c,
** xsurf100.c) defines its backend, then #includes "dp8390.c", so each
** driver object carries a private, all-static copy and any set of DP8390
** drivers links into one kernel without a symbol clash.  The driver's only
** global symbols are its own glue: <drv>info (the streamtab, from
** dp_rinit/dp_winit), <drv>intr (calls dp_intr) and <drv>init (calls
** dp_autoconfig).
**
** Before including dp8390.h the backend defines:
**   DP_NAME           interface name ("hya", "xsf"), also the module name
**   DP_MODID          STREAMS module id
**   DP_IOC            the user ioctl base ('H'<<8, ...): DP_IOC|1..5, |20
**   DP_MAXBOARDS      boards per driver
**   DP_BOARD_ID       the AutoConfig manufacturer<<16 | product autocon() takes
**   DP_TX_START       first page of the transmit buffer (6 pages are used)
**   DP_RX_START       first page of the receive ring
**   DP_STOP           first page after the receive ring
**   DP_RAM_START      first page of the card's buffer RAM (test ioctls)
**   DP_DCR            the Data Configuration Register value in operation
**   DP_RCR            the Receive Configuration Register value (AB|AM|...)
** and the types dp_status_t, dp_test_t, dp_config_t: its user header's
** status/test/config structs, which must have the fields of hydrauser.h.
**
** Then, before including dp8390.c, the backend's functions (dp_board_t is
** defined here):
**   static unsigned char dp_inb(dp_board_t *, int reg)
**   static void dp_outb(dp_board_t *, int reg, int val)
**   static int  dp_be_addr_ok(long addr, long size)   AutoConfig sanity
**   static int  dp_be_map(dp_board_t *)       board_base/nic_base; 0 or errno
**   static void dp_be_reset(dp_board_t *)     hardware reset, NIC stopped
**   static void dp_be_start(dp_board_t *)     after reset, before the setup
**   static void dp_be_stop(dp_board_t *)      after the NIC is stopped (close)
**   static void dp_be_get_mac(dp_board_t *, unsigned char mac[6])
**   static int  dp_be_ram_read(dp_board_t *, unsigned char *, int len, int off)
**   static int  dp_be_ram_write(dp_board_t *, unsigned char *, int len, int off)
**                                             1 = done, 0 = failed (DMA)
**   static int  dp_be_irq_pending(dp_board_t *)  0 = this board is quiet
**   static int  dp_be_test_reg_ok(dp_board_t *, int reg, int write)
**                                             test ioctl filter (may prepare)
*/
#ifndef _AMIGA_DP8390_H
#define _AMIGA_DP8390_H

#include "dp8390reg.h"

#define DP_MAXDEV	    5		/* streams (SAPs) per board */

#define DP_BUFLEN	    1518	/* frame + CRC, the receive limit */
#define DP_MTU		    1500

#define DP_MINPSZ	    (60 - 4)
#define DP_MAXPSZ	    DP_MTU
#define DP_HIWAT	    (1024 * 5)
#define DP_LOWAT	    (2 * DP_HIWAT / 3)

#define spldp		    spl2

#define DP_TX_PAGES	    6		/* one 1536-byte transmit buffer */

#define ETH_ADDRESS_SIZE    6
#define ETH_TYPE_SIZE       2
#define ETH_HEADER_SIZE     (ETH_ADDRESS_SIZE + ETH_ADDRESS_SIZE + ETH_TYPE_SIZE)
#define ETH_MAXDATA         1500
#define ETH_MAXPACKET       (ETH_HEADER_SIZE + ETH_MAXDATA)
#define ETH_MINFRAME        60		/* without CRC */
#define ETHERTYPE_IP        0x0800
#define ETHERTYPE_ARP       0x0806
#define ETHERTYPE_TRAIL     0x1000
#define ETHERTYPE_NTRAILER  16

typedef u_char ether_addr_t[6];

typedef struct
{
    ether_addr_t	ether_dhost;
    ether_addr_t	ether_shost;
    u_short		ether_type;
} ether_header_t;

typedef struct
{
    queue_t		*q;
    int			state;
    int			board_index;
    int			flags;
    unsigned short	sap;
    int			bufcall_id;	/* a pending bufcall(), or 0 */
} dp_t;

typedef struct
{
    volatile unsigned char *board_base;	/* kernel VA of the board */
    volatile unsigned char *nic_base;	/* kernel VA of DP8390 register 0 */
    unsigned long	board_phys;	/* AutoConfig address */
    long		board_size;	/* AutoConfig size (0: unknown) */
    int			mapped;		/* backend: how board_base was made */
    unsigned char	paddress[6];
    int			next_pkt;
} dp_info_t;

typedef struct
{
    dp_t		dp[DP_MAXDEV];
    dp_status_t		status;
    dp_info_t		info;
    struct ifstats	ifstats;
    int			if_flags;
} dp_board_t;

#define DELAY(x) { int _d = (x) * 100; while (_d--) ; }
#define blklen(bp)	((bp)->b_wptr - (bp)->b_rptr)

#endif
