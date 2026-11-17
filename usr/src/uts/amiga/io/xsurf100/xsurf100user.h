#ifndef _AMIGA_XSURF100USER_H
#define _AMIGA_XSURF100USER_H

/*
** xsurf100user.h -- the user interface of the X-Surf 100 driver (/dev/xsf0):
** the ioctls and structs of the shared DP8390 core, the same layout as
** the Hydra's hydrauser.h, with the ioctl base 'X'.
*/

#define XSURF100IOC	('X'<<8)
#define XSURF100_CLEAR_STATUS	(XSURF100IOC|1)
#define XSURF100_GET_STATUS	(XSURF100IOC|2)
#define XSURF100_GET_CONFIG	(XSURF100IOC|3)
#define XSURF100_SET_CONFIG	(XSURF100IOC|4)
#define XSURF100_NUMBER_OF_BOARDS	(XSURF100IOC|5)

#define XSURF100_RAW_DATA		(1)

#define XSURF100_IOCTL_TEST	(XSURF100IOC|20)

#define XSURF100_TEST_READ_REG	0
#define XSURF100_TEST_WRITE_REG	1
#define XSURF100_TEST_READ_RAM	2
#define XSURF100_TEST_WRITE_RAM	3
#define XSURF100_TEST_GET_STATE	4
#define XSURF100_TEST_TX		5
#define XSURF100_TEST_GET_BOARD	6

typedef struct
{
    int			op;
    unsigned char	reg;
    unsigned char	val;
    int			page;
    int			offset;
    int			len;
    unsigned char	data[1024];
    unsigned char	tsr, isr, curr, bnry;
    unsigned char	rsr, ncr, tcr, rcr;
} xsurf100_test_t;

enum XSURF100_BOARD_STATE
{
    XSURF100_BOARD_RESET,
    XSURF100_BOARD_RUNNING
};

typedef struct
{
    enum XSURF100_BOARD_STATE	board_state;
    unsigned int		packets_sent;
    unsigned int		packets_received;
    unsigned int		allocbs_failed;
    unsigned int		couldnt_put;
    unsigned int		tx_errors;
    unsigned int		rx_errors;
    unsigned int		collisions;
    unsigned int		overflow;
    unsigned int		crc_errors;
    unsigned int		framing_errors;
    unsigned int		missed_packets;
    unsigned int		buffer_error;
    unsigned int		trailers;
    unsigned int		board_debug;
    unsigned int		late_collisions;
    unsigned int		loss_of_carrier;
    unsigned int		retry_errors;
    unsigned int		rx_delivered;
    unsigned int		rx_no_sap;
    unsigned int		rx_not_for_us;
    unsigned int		last_rx_sap;
    unsigned int		last_bound_sap;
    unsigned int		open_streams;
    unsigned int		bound_streams;
    unsigned int		arp_streams;
    unsigned int		ip_streams;
    unsigned int		if_active;
    unsigned int		if_ipackets;
    unsigned int		if_opackets;
    unsigned int		if_ierrors;
    unsigned int		if_oerrors;
    unsigned int		tx_arp;
    unsigned int		tx_ip;
    unsigned int		rx_arp_delivered;
    unsigned int		rx_ip_delivered;
    unsigned int		last_tx_sap;
    unsigned int		rx_arp_seen;
    unsigned int		rx_ip_seen;
    unsigned int		sw_next_pkt;
    unsigned int		last_ring_page;
    unsigned int		last_ring_next;
    unsigned int		last_ring_rsr;
    unsigned int		last_ring_size;
    unsigned int		last_curr;
    unsigned int		last_bnry;
    unsigned int		rx_bad_next;
    unsigned int		rx_bad_rsr;
    unsigned int		rx_bad_size;
} xsurf100_status_t;

typedef struct
{
    long			board_base;
    int				board_debug;
    unsigned char		paddress[6];
    unsigned char		laddress[8];
    int				mode;
    int				flags;
} xsurf100_config_t;

#endif
