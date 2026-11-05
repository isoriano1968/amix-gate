/*
 * core1_net.h -- core 1's own network (amix-gate core1, lwIP): core 0 owns the
 * GEM and gives core 1 a second MAC address beside the Amiga's (the GEM's
 * specific address 2).  Frames to it, and broadcasts, go into core 1's RX ring;
 * core 1 queues frames in its TX ring and core 0's main loop sends them.  The
 * Amiga's path is unchanged.  The same file is in amix-gate's core1 tree.
 *
 * One megabyte of board memory, uncached on both cores.  Words are the ARM's
 * (little-endian): the 68k does not look here.  Ring indices count frames
 * without wrapping; slot = index % slots; a ring is full when wr - rd == slots.
 * A slot: word 0 the frame's length, the frame from byte C1NET_DATA.
 */
#ifndef CORE1_NET_H
#define CORE1_NET_H

#define C1NET_OFF          0x07900000UL    /* board offset */
#define C1NET_MAGIC        0x43314E54UL    /* "C1NT": core 0 sorts frames */

enum {
   C1NET_W_MAGIC,       /* core 0, at start */
   C1NET_W_ON,          /* core 1: 1 = give me my frames (core 0 then sets the GEM's address 2) */
   C1NET_W_MACLO,       /* core 0: core 1's MAC, bytes 0-3 (byte 0 lowest), */
   C1NET_W_MACHI,       /*         bytes 4-5 */
   C1NET_W_RXWR,        /* core 0 */
   C1NET_W_RXRD,        /* core 1 */
   C1NET_W_TXWR,        /* core 1 */
   C1NET_W_TXRD,        /* core 0 */
   C1NET_W_RXFRAMES,    /* core 0: into the RX ring */
   C1NET_W_RXDROPS,     /*         lost, the ring full */
   C1NET_W_TXFRAMES,    /*         sent */
   C1NET_W_TXERRS,      /*         not sent (ethernet_send's error) */
   C1NET_W_NWORDS
};

/* core 1's own report, for AMIX's monitors (libamixstat); core 0 does not
   touch these, a core 1 image clears them when it starts.  IP, NETMASK and GW
   are in network order (read as a big-endian word on the 68k: a.b.c.d is
   0xaabbccdd), the rest are little-endian like the words above. */
enum {
   C1NET_W_UP = 32,     /* 1 while a core with the network runs (DHCP may still be asking) */
   C1NET_W_IP,          /* DHCP's address, 0 none yet */
   C1NET_W_NETMASK,
   C1NET_W_GW,
   C1NET_W_RXBYTES,     /* frames' bytes taken / sent by core 1, wrapping */
   C1NET_W_TXBYTES,
   C1NET_W_REPORT_END
};

#define C1NET_SLOT         2048UL
#define C1NET_DATA         64UL            /* the frame's offset in a slot (cache line, GEM DMA) */
#define C1NET_RX_OFF       (C1NET_OFF + 0x10000UL)
#define C1NET_RX_SLOTS     192UL           /* 384 KB */
#define C1NET_TX_OFF       (C1NET_OFF + 0x80000UL)
#define C1NET_TX_SLOTS     64UL            /* 128 KB */

#endif
