#ifndef _AMIGA_HYRAUSER_H
#define _AMIGA_HYRAUSER_H

#define HYDRAIOC		('H'<<8)
#define HYDRA_CLEAR_STATUS	(HYDRAIOC|1)
#define HYDRA_GET_STATUS	(HYDRAIOC|2)
#define HYDRA_GET_CONFIG	(HYDRAIOC|3)
#define HYDRA_SET_CONFIG	(HYDRAIOC|4)
#define HYDRA_NUMBER_OF_BOARDS	(HYDRAIOC|5)

#define HYDRA_RAW_DATA		(1)

#define HYDRA_IOCTL_TEST	(HYDRAIOC|20)

#define HYDRA_TEST_READ_REG	0
#define HYDRA_TEST_WRITE_REG	1
#define HYDRA_TEST_READ_RAM	2
#define HYDRA_TEST_WRITE_RAM	3
#define HYDRA_TEST_GET_STATE	4
#define HYDRA_TEST_TX		5
#define HYDRA_TEST_GET_BOARD	6

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
} hydra_test_t;

enum HYDRA_BOARD_STATE
{
    HYDRA_BOARD_RESET,
    HYDRA_BOARD_RUNNING
};

typedef struct
{
    enum HYDRA_BOARD_STATE	board_state;
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
} hydra_status_t;

typedef struct
{
    long			board_base;
    int				board_debug;
    unsigned char		paddress[6];
    unsigned char		laddress[8];
    int				mode;
    int				flags;
} hydra_config_t;

#endif
