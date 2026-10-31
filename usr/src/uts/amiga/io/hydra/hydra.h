#ifndef _AMIGA_HYDRA_H
#define _AMIGA_HYDRA_H

#include "ne2000.h"
#include "hydrauser.h"

#define HYDRA_MAXDEV	    5
#define HYDRA_MAXBOARDS	    4

#define MAX_BUFFER_LENGTH   (1518)
#define HYDRA_MTU           (MAX_BUFFER_LENGTH - 14 - 4)

#define HYDRA_MIN	    (60 - 4)
#define HYDRA_MAX	    HYDRA_MTU
#define HYDRA_LOWAT	    (2 * HYDRA_HIWAT / 3)
#define HYDRA_HIWAT	    (1024 * 5)

#define HYDRA_INIT_PRI	    (PZERO - 1)

#define splhydra	    spl2

#define ETH_ADDRESS_SIZE    6
#define ETH_TYPE_SIZE       2
#define ETH_CRC_LEN         4
#define ETH_LENGTH_SIZE     2
#define ETH_HEADER_SIZE     (ETH_ADDRESS_SIZE + ETH_ADDRESS_SIZE + ETH_TYPE_SIZE)
#define ETH_MAXDATA         1500
#define ETH_MAXPACKET       (ETH_HEADER_SIZE + ETH_MAXDATA)
#define ETH_MINDATA         50
#define ETH_MINPACKET       (ETH_HEADER_SIZE + ETH_MINDATA)
#define ETH_MINFRAME        (ETH_MINPACKET - ETH_CRC_LEN)
#define ETHERTYPE_IP        0x0800
#define ETHERTYPE_ARP       0x0806

typedef u_char ether_addr_t[6];

struct ether_header
{
    ether_addr_t ether_dhost;
    ether_addr_t ether_shost;
    u_short	    ether_type;
};

typedef struct ether_header ether_header_t;

#define ETHERTYPE_TRAIL     0x1000
#define ETHERTYPE_NTRAILER  16

typedef struct
{
    queue_t         *q;
    int             state;
    int             board_index;
    int             flags;
    unsigned short  sap;
} hydra_t;

typedef struct
{
    unsigned char   *board_base;
    volatile unsigned char *nic_base;
    unsigned char   paddress[6];
    unsigned char   laddress[8];
    int             tx_start_page;
    int             rx_start_page;
    int             stop_page;
    int             next_pkt;
} hydra_info_t;

typedef struct
{
    hydra_t         hydra[HYDRA_MAXDEV];
    hydra_status_t  hydra_status;
    hydra_info_t    hydra_info;
    struct ifstats  ifstats;
    int             if_flags;
} hydra_board_t;

typedef struct
{
    long            address;
    int             type;
} hydra_autoconfig_t;

static void hydra_outb(volatile unsigned char *base, int reg, unsigned char val);
static unsigned char hydra_inb(volatile unsigned char *base, int reg);
static void hydra_outw(volatile unsigned char *base, int reg, unsigned short val);

#define DELAY(x) { int _d = (x) * 100; while (_d--) ; }
#define blklen(bp)	(bp->b_wptr - bp->b_rptr)

#endif
