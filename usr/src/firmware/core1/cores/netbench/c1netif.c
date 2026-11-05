/*
 * c1netif.c -- core 1's network interface for lwIP: frames from the RX ring
 * core 0 fills (its frames and the broadcasts), frames into the TX ring core 0
 * sends; the MAC is core 0's choice (core1_net.h).  Polled: c1net_service()
 * takes what came and runs lwIP's timers.  DHCP gives the address.
 */
#include <stdio.h>
#include <string.h>
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include "core1_net.h"
#include "player.h"
#include "c1netif.h"

#define RTG_BASE        0x18000000UL
#define RX_PER_CALL     64              /* frames taken per c1net_service */

static volatile uint32_t *const c1n = (volatile uint32_t *)(RTG_BASE + C1NET_OFF);
static struct netif nif;
static int started;

u32_t sys_now(void)
{
    return (u32_t)(pl_ticks() / (pl_tick_hz / 1000));
}

static void dmb(void)
{
    __asm__ volatile("dmb sy" ::: "memory");
}

static void mac_get(uint8_t *m)
{
    uint32_t lo = c1n[C1NET_W_MACLO], hi = c1n[C1NET_W_MACHI];

    m[0] = (uint8_t)lo;
    m[1] = (uint8_t)(lo >> 8);
    m[2] = (uint8_t)(lo >> 16);
    m[3] = (uint8_t)(lo >> 24);
    m[4] = (uint8_t)hi;
    m[5] = (uint8_t)(hi >> 8);
}

/* a frame into the TX ring; core 0 sends a few per turn of its main loop */
static err_t c1_out(struct netif *n, struct pbuf *p)
{
    const uint32_t wr = c1n[C1NET_W_TXWR];
    const uint64_t t0 = pl_ticks();
    volatile uint8_t *slot;

    (void)n;
    if (p->tot_len > C1NET_SLOT - C1NET_DATA)
        return ERR_BUF;
    while (wr - c1n[C1NET_W_TXRD] >= C1NET_TX_SLOTS)
        if (pl_ticks() - t0 > pl_tick_hz / 100)
            return ERR_MEM;             /* 10 ms: core 0 does not send */
    slot = (volatile uint8_t *)(RTG_BASE + C1NET_TX_OFF + (wr % C1NET_TX_SLOTS) * C1NET_SLOT);
    pbuf_copy_partial(p, (void *)(slot + C1NET_DATA), p->tot_len, 0);
    *(volatile uint32_t *)slot = p->tot_len;
    dmb();                              /* the frame before the index */
    c1n[C1NET_W_TXWR] = wr + 1;
    c1n[C1NET_W_TXBYTES] += p->tot_len;
    return ERR_OK;
}

/* the frames core 0 put into the RX ring, to lwIP */
static void c1_in(void)
{
    uint32_t rd = c1n[C1NET_W_RXRD];
    const uint32_t wr = c1n[C1NET_W_RXWR];
    int k;

    if (wr - rd > C1NET_RX_SLOTS)       /* lost count: from where core 0 is */
        rd = wr;
    for (k = 0; rd != wr && k < RX_PER_CALL; k++, rd++) {
        const volatile uint8_t *slot;
        uint32_t len;
        struct pbuf *p;

        dmb();                          /* the index before the frame */
        slot = (const volatile uint8_t *)(RTG_BASE + C1NET_RX_OFF + (rd % C1NET_RX_SLOTS) * C1NET_SLOT);
        len = *(const volatile uint32_t *)slot;
        if (len < 14 || len > C1NET_SLOT - C1NET_DATA)
            continue;
        p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
        if (!p)
            continue;                   /* out of pbufs: TCP sends it again */
        pbuf_take(p, (const void *)(slot + C1NET_DATA), (u16_t)len);
        c1n[C1NET_W_RXBYTES] += len;
        if (nif.input(p, &nif) != ERR_OK)
            pbuf_free(p);
    }
    c1n[C1NET_W_RXRD] = rd;
}

static err_t c1_init(struct netif *n)
{
    n->name[0] = 'c';
    n->name[1] = '1';
    n->hwaddr_len = 6;
    mac_get(n->hwaddr);
    n->mtu = 1500;
    n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    n->output = etharp_output;
    n->linkoutput = c1_out;
    n->hostname = "z3660-core1";
    return ERR_OK;
}

int c1net_start(void)
{
    ip4_addr_t z;

    if (started)
        return 0;
    if (c1n[C1NET_W_MAGIC] != C1NET_MAGIC)
        return -1;                      /* firmware without C1LD_FEAT_NET */
    c1n[C1NET_W_RXRD] = c1n[C1NET_W_RXWR];      /* what came before us is not ours */
    c1n[C1NET_W_TXWR] = c1n[C1NET_W_TXRD];
    for (int w = C1NET_W_UP; w < C1NET_W_REPORT_END; w++)
        c1n[w] = 0;
    lwip_init();
    ip4_addr_set_zero(&z);
    netif_add(&nif, &z, &z, &z, NULL, c1_init, ethernet_input);
    netif_set_default(&nif);
    netif_set_up(&nif);
    netif_set_link_up(&nif);
    c1n[C1NET_W_ON] = 1;
    c1n[C1NET_W_UP] = 1;
    dhcp_start(&nif);
    started = 1;
    return 0;
}

void c1net_service(void)
{
    uint8_t m[6];

    if (!started) {
        c1net_start();
        return;
    }
    if (c1n[C1NET_W_ON] != 1)           /* core 0 started anew (power-on words) */
        c1n[C1NET_W_ON] = 1;
    mac_get(m);
    if (memcmp(m, nif.hwaddr, 6)) {     /* AMIX set its MAC: ours follows; a new lease */
        memcpy(nif.hwaddr, m, 6);
        dhcp_stop(&nif);
        dhcp_start(&nif);
    }
    c1_in();
    sys_check_timeouts();
    /* the address for AMIX's monitors, as lwIP holds it (network order) */
    c1n[C1NET_W_IP] = c1net_addr();
    c1n[C1NET_W_NETMASK] = ip4_addr_get_u32(netif_ip4_netmask(&nif));
    c1n[C1NET_W_GW] = ip4_addr_get_u32(netif_ip4_gw(&nif));
}

uint32_t c1net_addr(void)
{
    return started && dhcp_supplied_address(&nif) ? ip4_addr_get_u32(netif_ip4_addr(&nif)) : 0;
}

void c1net_info(char *out, int n)
{
    char a[16], m[16], g[16];

    if (!started) {
        snprintf(out, n, "no network: %s", c1n[C1NET_W_MAGIC] != C1NET_MAGIC ?
                 "the firmware has no rings for core 1" : "not started");
        return;
    }
    ip4addr_ntoa_r(netif_ip4_addr(&nif), a, sizeof a);
    ip4addr_ntoa_r(netif_ip4_netmask(&nif), m, sizeof m);
    ip4addr_ntoa_r(netif_ip4_gw(&nif), g, sizeof g);
    snprintf(out, n, "%s %s/%s gw %s, mac %02x:%02x:%02x:%02x:%02x:%02x, rx %lu (drops %lu) tx %lu (errors %lu)",
             dhcp_supplied_address(&nif) ? "dhcp" : "waiting for dhcp:", a, m, g,
             nif.hwaddr[0], nif.hwaddr[1], nif.hwaddr[2], nif.hwaddr[3], nif.hwaddr[4], nif.hwaddr[5],
             (unsigned long)c1n[C1NET_W_RXFRAMES], (unsigned long)c1n[C1NET_W_RXDROPS],
             (unsigned long)c1n[C1NET_W_TXFRAMES], (unsigned long)c1n[C1NET_W_TXERRS]);
}
