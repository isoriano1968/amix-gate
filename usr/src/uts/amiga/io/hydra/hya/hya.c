/*
** hya - Hydra Systems AmigaNet control utility
**
** -S		Silent check (exit 0 if board found, 1 if not)
** -c		Get configuration (MAC, board base)
** -n		Print number of boards
** -d devname	Set device name (default /dev/hya0)
*/

#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "hydrauser.h"
#include "../ne2000.h"

static char *program_name, device_name[256];
static void hex_dump(unsigned char *buf, int len);

static int
do_ioctl_test(int fd, hydra_test_t *t)
{
    return ioctl(fd, HYDRA_IOCTL_TEST, t);
}

int
main(int argc, char **argv)
{
    int c, error_flag = 0;
    int number_of_boards_p = 0, get_config_p = 0, silent_p = 0, diag_p = 0;
    int fd, count;
    hydra_config_t cfg;

    program_name = *argv;

    while ((c = getopt(argc, argv, "SncDd:?")) != EOF)
    {
	switch (c)
	{
	case 'S':
	    silent_p++;
	    number_of_boards_p++;
	    break;

	case 'n':
	    number_of_boards_p++;
	    break;

	case 'c':
	    get_config_p++;
	    break;

	case 'D':
	    diag_p++;
	    break;

	case 'd':
	    if (optarg[0] == '/')
		strcpy(device_name, optarg);
	    else
		sprintf(device_name, "/dev/%s", optarg);
	    break;

	case '?':
	    error_flag++;
	    break;
	}

	if (error_flag)
	{
	    fprintf(stderr, "usage: %s [-SncD?] [-d dev]\n", program_name);
	    return 1;
	}
    }

    if (device_name[0] == '\0')
	strcpy(device_name, "/dev/hya0");

    fd = open(device_name, O_RDONLY);
    if (fd < 0)
    {
	if (!silent_p)
	    fprintf(stderr, "%s: couldn't open %s (%s)\n",
		    program_name, device_name, strerror(errno));

	if (number_of_boards_p)
	{
	    if (!silent_p)
		printf("0\n");
	    return 1;
	}

	return 1;
    }

    if (number_of_boards_p)
    {
	if (ioctl(fd, HYDRA_NUMBER_OF_BOARDS, &count) < 0)
	    count = 0;

	if (!silent_p)
	    printf("%d\n", count);

	close(fd);
	return count == 0;
    }

    if (ioctl(fd, HYDRA_GET_CONFIG, &cfg) < 0)
    {
	fprintf(stderr, "%s: couldn't get config from %s (%s)\n",
		program_name, device_name, strerror(errno));
	close(fd);
	return 1;
    }

    printf("Device:\t\t%s\n", device_name);

    printf("Ethernet:\t%02x:%02x:%02x:%02x:%02x:%02x\n",
	   cfg.paddress[0], cfg.paddress[1], cfg.paddress[2],
	   cfg.paddress[3], cfg.paddress[4], cfg.paddress[5]);

    if (get_config_p)
    {
	printf("Board Base:\t0x%lx\n", cfg.board_base);
	printf("Board Debug:\t0x%x\n", cfg.board_debug);
    }

    if (diag_p)
    {
	hydra_test_t t;
	int s_curr = 0, s_bnry = 0;

	/* Board state + MAC via GET_BOARD */
	memset(&t, 0, sizeof(t));
	t.op = HYDRA_TEST_GET_BOARD;
	if (do_ioctl_test(fd, &t) == 0)
	    printf("\nBoard State:\t%s (MAC %02x:%02x:%02x:%02x:%02x:%02x)\n",
		   t.data[6] == HYDRA_BOARD_RUNNING ? "RUNNING" : "RESET",
		   t.data[0], t.data[1], t.data[2],
		   t.data[3], t.data[4], t.data[5]);

	/* Registers via GET_STATE */
	memset(&t, 0, sizeof(t));
	t.op = HYDRA_TEST_GET_STATE;
	if (do_ioctl_test(fd, &t) == 0)
	{
	    s_curr = t.curr;
	    s_bnry = t.bnry;

	    printf("\nRegisters:\n");
	    printf("  ISR:\t\t0x%02x\n", t.isr);
	    printf("  TSR:\t\t0x%02x", t.tsr);
	    if (t.tsr & 0x01) printf(" PTX");
	    if (t.tsr & 0x02) printf(" DFR");
	    if (t.tsr & 0x04) printf(" COL");
	    if (t.tsr & 0x08) printf(" ABT");
	    if (t.tsr & 0x10) printf(" CRS");
	    if (t.tsr & 0x20) printf(" FU");
	    if (t.tsr & 0x40) printf(" CDH");
	    if (t.tsr & 0x80) printf(" OWC");
	    printf("\n");
	    printf("  RSR:\t\t0x%02x\n", t.rsr);
	    printf("  NCR:\t\t0x%02x\n", t.ncr);
	    printf("  CURR:\t\t0x%02x (page %d)\n", t.curr, t.curr);
	    printf("  BNRY:\t\t0x%02x (page %d)\n", t.bnry, t.bnry);

		if (t.curr != (unsigned char)(t.bnry + 1))
		{
		    int pending;
		    if (t.curr > t.bnry)
			pending = t.curr - t.bnry - 1;
		    else
			pending = NE8390_STOP_PG - t.bnry + t.curr - 1;
		    printf("  RX ring:\t%u packet(s) pending\n", pending);
		}
		else
		    printf("  RX ring:\tempty\n");
	}

	/* TX buffer dump */
	memset(&t, 0, sizeof(t));
	t.op = HYDRA_TEST_READ_RAM;
	t.page = 0;
	t.len = 60;
	if (do_ioctl_test(fd, &t) == 0)
	{
	    printf("\nTX buffer (page 0):\n");
	    hex_dump(t.data, t.len);
	}

	/* RX ring */
	{
	    int rx_page = s_bnry + 1;
	    if (rx_page >= NE8390_STOP_PG)
		rx_page = NE8390_RX_START_PG;

	    memset(&t, 0, sizeof(t));
	    t.op = HYDRA_TEST_READ_RAM;
	    t.page = rx_page;
	    t.len = 64;
	    if (do_ioctl_test(fd, &t) == 0)
	    {
		printf("\nRX ring header:\n");
		hex_dump(t.data, 4);
		printf("\nRX packet data:\n");
		hex_dump(t.data + 4, 60);
	    }
	}
    }

    close(fd);
    return 0;
}

static void
hex_dump(unsigned char *buf, int len)
{
    int i, j;
    for (i = 0; i < len; i += 16)
    {
	printf("  %04x: ", i);
	for (j = 0; j < 16 && i + j < len; j++)
	    printf("%02x%c", buf[i + j], (j == 7) ? '-' : ' ');
	printf("  ");
	for (j = 0; j < 16 && i + j < len; j++)
	    printf("%c", buf[i + j] >= 32 && buf[i + j] < 127 ?
		   buf[i + j] : '.');
	printf("\n");
    }
}
