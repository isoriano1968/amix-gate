/*
 * ztop.c -- Z3660 board sensors on AMIX, the readings of the Z3660's AmigaOS
 * ZTop (shanshe/Z3660 z3660-drivers/ZTop/Ztop.c): Zynq (ARM + FPGA) die
 * temperature and supply voltages, the LTC2990 board monitor (board
 * temperature, 3.3 V and 5 V rails, the 68060's thermistor), the CPU clock.
 *
 * The registers sit in the RTG register window the zz9000 driver already
 * maps; ZZ9KIOC_GETREG reads one aligned 32-bit register and returns its
 * high half at the register's offset and its low half at offset + 2.
 * Offsets and scaling as in z3660-drivers/common/z3660_regs.h and Ztop.c.
 *
 * usage: ztop [-l [seconds]] [device]      (device default /dev/zz9000)
 */
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../zz9000.h"

#define REG_ZZ_FW_VERSION       0x1a0
#define REG_ZZ_TEMPERATURE      0x1d4	/* Zynq die, 0.1 C */
#define REG_ZZ_VOLTAGE_AUX      0x1d8	/* Zynq VCCAUX, 0.01 V */
#define REG_ZZ_VOLTAGE_INT      0x1dc	/* Zynq VCCINT, 0.01 V */
#define REG_ZZ_CPU_FREQ         0x1f8	/* MHz */
#define REG_ZZ_EMULATION_USED   0x208
#define REG_ZZ_JIT_ENABLE       0x20c
#define REG_ZZ_LTC_TEMP         0x220	/* LTC2990, 0.01 C */
#define REG_ZZ_LTC_V1           0x224	/* 3.3 V rail, 0.01 V */
#define REG_ZZ_LTC_V2           0x228	/* 5 V rail, 0.01 V */
#define REG_ZZ_LTC_060_TEMP     0x22c	/* 68060 thermistor, 0.01 C */
#define REG_ZZ_FW_BETA          0x280
#define REG_ZZ_FW_ALFA          0x284

static int fd;

static long
reg32(off)
int off;
{
    struct zz9k_reg hi, lo;

    hi.offset = off;
    lo.offset = off + 2;
    hi.value = lo.value = 0;
    if (ioctl(fd, ZZ9KIOC_GETREG, &hi) < 0 || ioctl(fd, ZZ9KIOC_GETREG, &lo) < 0) {
        perror("ZZ9KIOC_GETREG");
        exit(1);
    }
    return (long)(((unsigned long)hi.value << 16) | lo.value);
}

static void
show(loop)
int loop;
{
    long fw = reg32(REG_ZZ_FW_VERSION), beta = reg32(REG_ZZ_FW_BETA), alfa = reg32(REG_ZZ_FW_ALFA);

    if (!loop) {
        printf("Z3660 firmware %ld.%02ld", (fw >> 8) & 0xff, fw & 0xff);
        if (beta)
            printf(" beta %ld", beta);
        if (alfa)
            printf(" alfa %ld", alfa);
        printf(", CPU %ld MHz%s%s\n", reg32(REG_ZZ_CPU_FREQ),
            reg32(REG_ZZ_EMULATION_USED) ? ", emulated 68k" : "",
            reg32(REG_ZZ_JIT_ENABLE) ? " (JIT)" : "");
        printf("Zynq (ARM+FPGA) %5.1f C   VCCINT %4.2f V   VCCAUX %4.2f V\n",
            reg32(REG_ZZ_TEMPERATURE) * 0.1, reg32(REG_ZZ_VOLTAGE_INT) * 0.01,
            reg32(REG_ZZ_VOLTAGE_AUX) * 0.01);
        printf("68060 (therm)   %5.1f C   board %5.1f C   3V3 %4.2f V   5V %4.2f V\n",
            reg32(REG_ZZ_LTC_060_TEMP) * 0.01, reg32(REG_ZZ_LTC_TEMP) * 0.01,
            reg32(REG_ZZ_LTC_V1) * 0.01, reg32(REG_ZZ_LTC_V2) * 0.01);
    } else
        printf("Zynq %5.1f C  060 %5.1f C  board %5.1f C  VCCINT %4.2f  3V3 %4.2f  5V %4.2f  %ld MHz\n",
            reg32(REG_ZZ_TEMPERATURE) * 0.1, reg32(REG_ZZ_LTC_060_TEMP) * 0.01,
            reg32(REG_ZZ_LTC_TEMP) * 0.01, reg32(REG_ZZ_VOLTAGE_INT) * 0.01,
            reg32(REG_ZZ_LTC_V1) * 0.01, reg32(REG_ZZ_LTC_V2) * 0.01, reg32(REG_ZZ_CPU_FREQ));
    fflush(stdout);
}

int
main(argc, argv)
int argc;
char **argv;
{
    char *dev = "/dev/zz9000";
    int loop = 0, i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-l")) {
            loop = 2;
            if (i + 1 < argc && atoi(argv[i + 1]) > 0)
                loop = atoi(argv[++i]);
        } else
            dev = argv[i];
    }
    fd = open(dev, O_RDONLY);
    if (fd < 0) {
        perror(dev);
        return 1;
    }
    show(0);
    while (loop) {
        sleep(loop);
        show(1);
    }
    return 0;
}
