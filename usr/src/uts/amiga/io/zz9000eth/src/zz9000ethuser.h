#ifndef _AMIGA_ZZ9000ETHUSER_H
#define _AMIGA_ZZ9000ETHUSER_H
/*
 * zz9000ethuser.h -- user-facing definitions (ioctls + status/config structs)
 * for the MNT ZZ9000 AMIX ethernet driver.  Contains NO kernel types so a
 * userland status/diagnostic tool can include it directly (mirrors hydrauser.h).
 */

#define ZZ9000ETHIOC		('z'<<8)	/* 'Z' is zz9000's and z3660eth's */
#define ZZ9000ETH_CLEAR_STATUS	(ZZ9000ETHIOC|1)
#define ZZ9000ETH_GET_STATUS	(ZZ9000ETHIOC|2)
#define ZZ9000ETH_GET_CONFIG	(ZZ9000ETHIOC|3)
#define ZZ9000ETH_NUMBER_OF_BOARDS (ZZ9000ETHIOC|5)

#define ZZ9000ETH_RAW_DATA	(1)

enum ZZ9000ETH_BOARD_STATE
{
    ZZ9000ETH_BOARD_RESET,
    ZZ9000ETH_BOARD_RUNNING
};

/*
 * Runtime counters, readable via ZZ9000ETH_GET_STATUS (and the diagnostic
 * globals below).  Kept deliberately verbose like the Hydra driver so the
 * datapath can be characterised over the serial console / a /dev probe during
 * the real-HW bring-up (as z3660eth's, z3660net ETHERNET-SCOPING.md).
 */
typedef struct
{
    unsigned int	board_state;
    unsigned int	packets_sent;
    unsigned int	packets_received;
    unsigned int	allocbs_failed;
    unsigned int	couldnt_put;
    unsigned int	tx_errors;	/* trigger returned rc 2/3/4 */
    unsigned int	tx_nolink;	/* trigger returned rc 1 (no carrier) */
    unsigned int	rx_errors;
    unsigned int	rx_delivered;
    unsigned int	rx_no_sap;
    unsigned int	rx_not_for_us;
    unsigned int	rx_bad_size;
    unsigned int	rx_overrun;	/* slot pointer past the 32-slot ring */
    unsigned int	last_rx_sap;
    unsigned int	last_tx_sap;
    unsigned int	last_bound_sap;
    unsigned int	last_serial;
    unsigned int	last_rx_len;
    unsigned int	tx_arp;
    unsigned int	tx_ip;
    unsigned int	rx_arp_seen;
    unsigned int	rx_ip_seen;
    unsigned int	drain_calls;	/* poll/ISR drain invocations */
    unsigned int	if_ipackets;
    unsigned int	if_opackets;
    unsigned int	if_ierrors;
    unsigned int	if_oerrors;
} zz9000eth_status_t;

typedef struct
{
    long		board_base;
    unsigned char	paddress[6];
    int			mode;
    int			flags;
} zz9000eth_config_t;

#endif /* _AMIGA_ZZ9000ETHUSER_H */
