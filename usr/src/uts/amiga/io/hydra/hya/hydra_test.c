/*
** hydra_test - Hydra diagnostic test program
**
** Opens /dev/hya0 and runs register/RAM/TX/RX tests.
** Logs to stdout and to a file.
**
** Usage: hydra_test [-d dev] [-l logfile]
*/

#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <varargs.h>
#include <poll.h>
#include "hydrauser.h"
#include "../ne2000.h"

#define HYDRA_TEST_BUILD "hydra_test build: rx-minframe-fix-2026-06-11"

static char device[256] = "/dev/hya0";
static int logfd = -1;
static int step;
static hydra_status_t last_status;
static int have_status;
static unsigned char board_mac[6];

static void log_printf(va_alist)
va_dcl
{
    va_list ap;
    char *fmt;
    char buf[4096];

    va_start(ap);
    fmt = va_arg(ap, char *);
    vsprintf(buf, fmt, ap);
    va_end(ap);

    printf("%s", buf);
    fflush(stdout);

    if (logfd >= 0)
    {
	write(logfd, buf, strlen(buf));
	fsync(logfd);
    }
}

static void hex_dump(char *label, unsigned char *buf, int len)
{
    int i, j;
    log_printf("%s (%d bytes):\n", label, len);
    for (i = 0; i < len; i += 16)
    {
	log_printf("  %04x: ", i);
	for (j = 0; j < 16 && i + j < len; j++)
	    log_printf("%02x%c", buf[i + j], (j == 7) ? '-' : ' ');
	log_printf("  ");
	for (j = 0; j < 16 && i + j < len; j++)
	    log_printf("%c", buf[i + j] >= 32 && buf[i + j] < 127 ? buf[i + j] : '.');
	log_printf("\n");
    }
}

static unsigned short be16(p)
unsigned char *p;
{
    return ((unsigned short)p[0] << 8) | p[1];
}

static int is_broadcast(p)
unsigned char *p;
{
    int i;

    for (i = 0; i < 6; i++)
	if (p[i] != 0xff)
	    return 0;
    return 1;
}

static int is_our_mac(p)
unsigned char *p;
{
    return memcmp((char *)p, (char *)board_mac, 6) == 0;
}

static void log_mac(p)
unsigned char *p;
{
    log_printf("%02x:%02x:%02x:%02x:%02x:%02x",
	       p[0], p[1], p[2], p[3], p[4], p[5]);
}

static void log_ip(p)
unsigned char *p;
{
    log_printf("%d.%d.%d.%d", p[0], p[1], p[2], p[3]);
}

static int test_ioctl(int fd, hydra_test_t *t)
{
    if (ioctl(fd, HYDRA_IOCTL_TEST, t) < 0)
    {
	log_printf("  *** ioctl failed: %s\n", strerror(errno));
	return -1;
    }
    return 0;
}

static void step_header(char *label)
{
    log_printf("\n======================================================================\n");
    log_printf("STEP %d: %s\n", ++step, label);
    log_printf("======================================================================\n");
}

static void msleep(int ms)
{
    poll(0, 0, ms);
}

static void test_board_info(int fd)
{
    hydra_test_t t;

    step_header("Board Info");

    memset(&t, 0, sizeof(t));
    t.op = HYDRA_TEST_GET_BOARD;
    if (test_ioctl(fd, &t) < 0)
	return;

    log_printf("  Board base: 0x%x\n", t.page);
    log_printf("  MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
	   t.data[0], t.data[1], t.data[2],
	   t.data[3], t.data[4], t.data[5]);
    memcpy(board_mac, t.data, 6);
    log_printf("  Board state: %s\n",
	   t.data[6] == 1 ? "RUNNING" : "RESET");
}

static void test_registers(int fd)
{
    step_header("Register Dump (page 0)");

    log_printf("  Skipped: live NIC register reads can trap on Hydra.\n");
    log_printf("  The FIFO/TBCR1 slot at 0xe9ffed is not touched.\n");
}

static void test_state(int fd)
{
    step_header("NIC State");

    log_printf("  Skipped: avoids live NIC register snapshot on Hydra.\n");
    log_printf("  Use Driver Status Counters and RX Ring Snapshot below.\n");
}

static void test_driver_status(int fd)
{
    hydra_status_t s;

    step_header("Driver Status Counters");

    memset(&s, 0, sizeof(s));
    if (ioctl(fd, HYDRA_GET_STATUS, &s) < 0)
    {
	log_printf("  *** status ioctl failed: %s\n", strerror(errno));
	return;
    }
    last_status = s;
    have_status = 1;

    log_printf("  Board state: %s\n",
	       s.board_state == HYDRA_BOARD_RUNNING ? "RUNNING" : "RESET");
    log_printf("  Sent=%u Received=%u TXerr=%u RXerr=%u\n",
	       s.packets_sent, s.packets_received, s.tx_errors, s.rx_errors);
    log_printf("  allocb_failed=%u couldnt_put=%u buffer_error=%u\n",
	       s.allocbs_failed, s.couldnt_put, s.buffer_error);
    log_printf("  overflow=%u crc=%u framing=%u missed=%u\n",
	       s.overflow, s.crc_errors, s.framing_errors, s.missed_packets);
    log_printf("  collisions=%u late=%u carrier=%u retry=%u trailers=%u\n",
	       s.collisions, s.late_collisions, s.loss_of_carrier,
	       s.retry_errors, s.trailers);
    log_printf("  delivered=%u no_sap=%u not_for_us=%u\n",
	       s.rx_delivered, s.rx_no_sap, s.rx_not_for_us);
    log_printf("  last_rx_sap=0x%04x last_bound_sap=0x%04x\n",
	       s.last_rx_sap, s.last_bound_sap);
    log_printf("  streams open=%u bound=%u arp=%u ip=%u\n",
	       s.open_streams, s.bound_streams, s.arp_streams, s.ip_streams);
    log_printf("  ifstats active=%u ipackets=%u opackets=%u ierrors=%u oerrors=%u\n",
	       s.if_active, s.if_ipackets, s.if_opackets,
	       s.if_ierrors, s.if_oerrors);
    log_printf("  tx_arp=%u tx_ip=%u rx_arp_delivered=%u rx_ip_delivered=%u\n",
	       s.tx_arp, s.tx_ip, s.rx_arp_delivered, s.rx_ip_delivered);
    log_printf("  rx_arp_seen=%u rx_ip_seen=%u\n",
	       s.rx_arp_seen, s.rx_ip_seen);
    log_printf("  last_tx_sap=0x%04x\n", s.last_tx_sap);
    log_printf("  sw_next_pkt=0x%02x last_ring page=0x%02x next=0x%02x rsr=0x%02x size=%u\n",
	       s.sw_next_pkt, s.last_ring_page, s.last_ring_next,
	       s.last_ring_rsr, s.last_ring_size);
    log_printf("  hw last_curr=0x%02x last_bnry=0x%02x bad_next=%u bad_rsr=%u bad_size=%u\n",
	       s.last_curr, s.last_bnry, s.rx_bad_next,
	       s.rx_bad_rsr, s.rx_bad_size);
}

static void test_ring_read(int fd, int page, char *label)
{
    hydra_test_t t;

    memset(&t, 0, sizeof(t));
    t.op = HYDRA_TEST_READ_RAM;
    t.page = page;
    t.len = 64;
    if (test_ioctl(fd, &t) < 0)
	return;

    log_printf("\n  Ring header at page 0x%02x (%s):\n", page, label);
    log_printf("    [0] Next=0x%02x  [1] RSR=0x%02x  [2] SizeLo=0x%02x  [3] SizeHi=0x%02x",
	   t.data[0], t.data[1], t.data[2], t.data[3]);
    log_printf("  -> pkt=%d bytes\n", (t.data[3] << 8) | t.data[2]);

    hex_dump("    bytes 0-63", t.data, 64);
}

static int read_ring_header(fd, page, t)
int fd, page;
hydra_test_t *t;
{
    memset(t, 0, sizeof(*t));
    t->op = HYDRA_TEST_READ_RAM;
    t->page = page;
    t->len = 64;
    return test_ioctl(fd, t);
}

static void describe_ring_page(page, data, prefix)
int page;
unsigned char *data;
char *prefix;
{
    int next, rsr, size, type, valid;
    unsigned char *dst, *src;

    next = data[0];
    rsr = data[1];
    size = ((int)data[3] << 8) | data[2];
    type = be16(&data[16]);
    valid = (next >= NE8390_RX_START_PG && next < NE8390_STOP_PG &&
	     size >= 60 && size <= 1518 && (rsr & 0x01));
    dst = &data[4];
    src = &data[10];

    log_printf("  %s page=0x%02x next=0x%02x rsr=0x%02x size=%4d",
	       prefix, page, next, rsr, size);
    log_printf(" type=0x%04x ", type);

    if (valid)
	log_printf("valid ");
    else
	log_printf("stale/invalid ");

    if (is_our_mac(dst))
	log_printf("to-us ");
    else if (is_broadcast(dst))
	log_printf("bcast ");
    else
	log_printf("other ");

    log_printf("dst=");
    log_mac(dst);
    log_printf(" src=");
    log_mac(src);

    if (type == 0x0806)
    {
	log_printf(" ARP op=%u sip=", be16(&data[24]));
	log_ip(&data[32]);
	log_printf(" tip=");
	log_ip(&data[42]);
    }
    else if (type == 0x0800)
    {
	log_printf(" IP src=");
	log_ip(&data[30]);
	log_printf(" dst=");
	log_ip(&data[34]);
    }

    log_printf("\n");
}

static void test_rx_ring_map(fd)
int fd;
{
    hydra_test_t t;
    int page, i, next, size, type, ok;
    int valid, arp, arp_to_us, ip, ip_to_us, bcast, invalid;
    int visited[NE8390_STOP_PG];

    step_header("RX Ring Full Map");
    log_printf("  Passive RAM-only scan of every RX page; no NIC registers touched.\n");
    if (have_status)
	log_printf("  Driver pointers: sw_next_pkt=0x%02x last_ring_page=0x%02x last_next=0x%02x\n",
		   last_status.sw_next_pkt, last_status.last_ring_page,
		   last_status.last_ring_next);

    valid = arp = arp_to_us = ip = ip_to_us = bcast = invalid = 0;

    for (page = NE8390_RX_START_PG; page < NE8390_STOP_PG; page++)
    {
	if (read_ring_header(fd, page, &t) < 0)
	    return;

	size = ((int)t.data[3] << 8) | t.data[2];
	type = be16(&t.data[16]);
	ok = (t.data[0] >= NE8390_RX_START_PG &&
	      t.data[0] < NE8390_STOP_PG &&
	      size >= 60 && size <= 1518 &&
	      (t.data[1] & 0x01));

	if (ok)
	{
	    valid++;
	    if (is_broadcast(&t.data[4]))
		bcast++;
	    if (type == 0x0806)
	    {
		arp++;
		if (is_our_mac(&t.data[4]))
		    arp_to_us++;
	    }
	    else if (type == 0x0800)
	    {
		ip++;
		if (is_our_mac(&t.data[4]))
		    ip_to_us++;
	    }
	}
	else
	    invalid++;

	describe_ring_page(page, t.data, "map");
    }

    log_printf("  Summary: valid=%d invalid=%d arp=%d arp_to_us=%d ip=%d ip_to_us=%d broadcast=%d\n",
	       valid, invalid, arp, arp_to_us, ip, ip_to_us, bcast);

    if (!have_status)
	return;

    step_header("RX Chain From Driver sw_next_pkt");
    for (i = 0; i < NE8390_STOP_PG; i++)
	visited[i] = 0;

    next = last_status.sw_next_pkt;
    for (i = 0; i < 16; i++)
    {
	if (next < NE8390_RX_START_PG || next >= NE8390_STOP_PG)
	{
	    log_printf("  chain stops: next page 0x%02x outside RX ring\n", next);
	    break;
	}
	if (visited[next])
	{
	    log_printf("  chain stops: page 0x%02x repeats\n", next);
	    break;
	}
	visited[next] = 1;

	if (read_ring_header(fd, next, &t) < 0)
	    return;
	describe_ring_page(next, t.data, "chain");
	next = t.data[0];
    }
}

static void test_ram_byte_word(int fd)
{
    hydra_test_t t;
    int i;
    unsigned char pattern[32], hexpat[16];
    int test_page;

    step_header("Buffer RAM Access Test");

    /*
    ** Keep the RAM test inside the TX buffer area.  Page 0 may contain the
    ** most recent transmit packet; pages 1 and 2 are still below RX start.
    */
    test_page = NE8390_START_PG + 1;

    /* Pattern: 0x00-0x1f */
    for (i = 0; i < 32; i++)
	pattern[i] = i;

    /* Write via word-wide ioctl */
    memset(&t, 0, sizeof(t));
    t.op = HYDRA_TEST_WRITE_RAM;
    t.page = test_page;
    t.len = 32;
    memcpy(t.data, pattern, 32);
    if (test_ioctl(fd, &t) < 0)
	return;

    /* Read back */
    memset(&t, 0, sizeof(t));
    t.op = HYDRA_TEST_READ_RAM;
    t.page = test_page;
    t.len = 32;
    if (test_ioctl(fd, &t) < 0)
	return;

    hex_dump("Write 0x00-0x1f, readback", t.data, 32);

    /* Check byte ordering */
    log_printf("  Byte order test (expected: 00 01 02 03 ...):\n    ");
    for (i = 0; i < 16; i++)
	log_printf(" %02x", t.data[i]);
    log_printf("\n");

    /* Check each position */
    for (i = 0; i < 16; i++)
    {
	if (t.data[i] != pattern[i])
	{
	    log_printf("  -> MISMATCH at offset %d: expected %02x got %02x\n",
		   i, pattern[i], t.data[i]);
	    break;
	}
    }
    if (i == 16)
	log_printf("  -> First 16 bytes MATCH (no swap)\n");

    /* Write known hex pattern */
    hexpat[0] = 0xDE; hexpat[1] = 0xAD; hexpat[2] = 0xBE; hexpat[3] = 0xEF;
    hexpat[4] = 0xCA; hexpat[5] = 0xFE; hexpat[6] = 0xBA; hexpat[7] = 0xBE;
    hexpat[8] = 0x12; hexpat[9] = 0x34; hexpat[10] = 0x56; hexpat[11] = 0x78;
    hexpat[12] = 0xAB; hexpat[13] = 0xCD; hexpat[14] = 0xEF; hexpat[15] = 0x00;

    memset(&t, 0, sizeof(t));
    t.op = HYDRA_TEST_WRITE_RAM;
    t.page = test_page + 1;  /* different TX-buffer page */
    t.len = 16;
    memcpy(t.data, hexpat, 16);
    if (test_ioctl(fd, &t) < 0)
	return;

    memset(&t, 0, sizeof(t));
    t.op = HYDRA_TEST_READ_RAM;
    t.page = test_page + 1;
    t.len = 16;
    if (test_ioctl(fd, &t) < 0)
	return;

    hex_dump("Write DEADBEEF..., readback", t.data, 16);

    /* Check byte ordering */
    log_printf("  Expected: DE AD BE EF CA FE BA BE 12 34 56 78 AB CD EF 00\n    ");
    for (i = 0; i < 16; i++)
	log_printf(" %02x", t.data[i]);
    log_printf("\n");

    for (i = 0; i < 16; i++)
    {
	if (t.data[i] != hexpat[i])
	{
	    log_printf("  -> MISMATCH at byte %d (expected %02x got %02x)\n",
		   i, hexpat[i], t.data[i]);
	    break;
	}
    }
    if (i == 16)
	log_printf("  -> All 16 bytes MATCH\n");
}

static void test_reg_write(int fd)
{
    step_header("Register Write Test (TPSR, TBCNT0, TBCNT1)");
    log_printf("  Skipped: raw TBCR1/FIFO access at 0xe9ffed traps on Hydra.\n");
    log_printf("  TX setup is exercised only by normal driver traffic.\n");
}

static void test_reg6_stopped(int fd)
{
    step_header("Register 6 Write with STP (stopped)");
    log_printf("  Skipped: Hydra traps on register 6/TBCR1 access while stopped.\n");
}

static void test_tx(int fd)
{
    step_header("TX Test");
    log_printf("  Skipped: synthetic HYDRA_TEST_TX can trap on TBCR1 access.\n");
    log_printf("  Use ping/arp traffic for TX/RX observation instead.\n");
}

static void test_rx_poll(int fd, int timeout_ms)
{
    int page;

    step_header("RX Ring Snapshot");
    log_printf("  Passive dump only; no NIC register polling.\n");

    for (page = NE8390_RX_START_PG; page < NE8390_RX_START_PG + 4; page++)
	test_ring_read(fd, page, "RX ring page");
}

int main(int argc, char **argv)
{
    int fd, c;
    char *logfile = "hydra_test.log";
    char dev[256] = "/dev/hya0";

    while ((c = getopt(argc, argv, "d:l:?")) != EOF)
    {
	switch (c)
	{
	case 'd':
	    if (optarg[0] == '/')
		strcpy(dev, optarg);
	    else
	    {
		strcpy(dev, "/dev/");
		strcat(dev, optarg);
	    }
	    break;
	case 'l':
	    logfile = optarg;
	    break;
	case '?':
	default:
	    fprintf(stderr, "usage: %s [-d dev] [-l logfile]\n", argv[0]);
	    return 1;
	}
    }

    logfd = open(logfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (logfd < 0)
    {
	fprintf(stderr, "can't open %s: %s\n", logfile, strerror(errno));
	return 1;
    }

    log_printf("hydra_test - Hydra diagnostic test program\n");
    log_printf("%s\n", HYDRA_TEST_BUILD);
    log_printf("Device: %s\n", dev);
    log_printf("Log file: %s\n", logfile);
    log_printf("\n");

    fd = open(dev, O_RDONLY);
    if (fd < 0)
    {
	log_printf("can't open %s: %s\n", dev, strerror(errno));
	if (logfd >= 0) close(logfd);
	return 1;
    }

    log_printf("Board opened OK, fd=%d\n", fd);

    test_board_info(fd);
    test_registers(fd);
    test_state(fd);
    test_driver_status(fd);
    test_rx_ring_map(fd);
    test_ram_byte_word(fd);
    test_ring_read(fd, 0, "TX page 0");
    test_ring_read(fd, NE8390_RX_START_PG, "RX start page");
    test_reg_write(fd);
    test_reg6_stopped(fd);
    test_tx(fd);
    test_rx_poll(fd, 10000);

    close(fd);
    log_printf("\nhydra_test complete\n");
    if (logfd >= 0) close(logfd);
    return 0;
}
