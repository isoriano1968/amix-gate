/*
 * lwipopts.h -- lwIP 2.1.3 on the Z3660's core 1: no OS (NO_SYS), polled from
 * the harness (c1net_service), IPv4 with DHCP, TCP and UDP, the raw API only.
 * Sized for one or two HTTP streams of video: a full 64 KB window, enough
 * pbufs for it in the RX ring's worth of frames.
 */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS                      1
#define LWIP_TIMERS                 1
#define SYS_LIGHTWEIGHT_PROT        0   /* one thread, no interrupts */
#define LWIP_NETCONN                0
#define LWIP_SOCKET                 0
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    0
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_DHCP                   1
#define LWIP_DNS                    1
#define LWIP_AUTOIP                 0
#define LWIP_IGMP                   0
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_STATS                  0
#define LWIP_PROVIDE_ERRNO          1
#define LWIP_CHKSUM_ALGORITHM       3
#define IP_REASSEMBLY               0
#define IP_FRAG                     0

/* memory: lwIP's own heap and pools, from core 1's big heap (malloc) */
#define MEM_LIBC_MALLOC             1
#define MEMP_MEM_MALLOC             1
#define MEM_ALIGNMENT               4
#define PBUF_POOL_SIZE              256
#define PBUF_POOL_BUFSIZE           1536
#define MEMP_NUM_TCP_PCB            8
#define MEMP_NUM_TCP_SEG            128
#define MEMP_NUM_PBUF               64

/* TCP: a full window (the A9 empties it at once), segments of a 1500 MTU */
#define TCP_MSS                     1460
#define TCP_WND                     (44 * TCP_MSS)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            32
#define TCP_QUEUE_OOSEQ             1
#define LWIP_TCP_SACK_OUT           0
#define LWIP_WND_SCALE              0
#define TCP_LISTEN_BACKLOG          0


#endif
