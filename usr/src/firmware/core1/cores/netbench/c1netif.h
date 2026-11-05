/*
 * c1netif.h -- core 1's network interface for lwIP over the rings core 0
 * fills and empties (core1_net.h), and what the harness calls.
 */
#ifndef C1NETIF_H
#define C1NETIF_H

#include <stdint.h>

/* the interface up, DHCP asked; 0, or < 0 if core 0 has no rings for us */
int c1net_start(void);
/* frames in, lwIP's timers: from the harness's idle loop and while waiting */
void c1net_service(void);
/* the address DHCP gave (network order), 0 none yet */
uint32_t c1net_addr(void);
/* "a.b.c.d/mask gw g, mac m, rx n (drops d) tx n (errors e)" */
void c1net_info(char *out, int n);

#endif
