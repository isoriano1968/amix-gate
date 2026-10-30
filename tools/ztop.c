/*
 * ztop.c -- Z3660 board sensors on AMIX, the readings of the Z3660's AmigaOS
 * ZTop (shanshe/Z3660 z3660-drivers/ZTop/Ztop.c): Zynq (ARM + FPGA) die
 * temperature and supply voltages, the LTC2990 board monitor (board
 * temperature, 3.3 V and 5 V rails, the 68060's thermistor), the CPU clock,
 * and what runs on the Zynq's second ARM core.
 *
 * The registers sit in the RTG register window the zz9000 driver already
 * maps; ZZ9KIOC_GETREG reads one aligned 32-bit register and returns its
 * high half at the register's offset and its low half at offset + 2.
 * Offsets and scaling as in z3660-drivers/common/z3660_regs.h and Ztop.c.
 *
 * Core 1 has no register of its own: the firmware only says whether the
 * 68040 emulator runs there.  A program on core 1 that publishes a status
 * block in board memory is shown from that block (read through mmap(), so
 * a ZZ9K_MAP_BOARD kernel); so far h264bench (vplay-amix arm/h264bench).
 *
 * usage: ztop [-l [seconds]] [device]      (device default /dev/zz9000)
 */
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include "zz9000.h"

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

/* h264bench's control block (vplay-amix arm/h264bench/bench.h): 32-bit
   big-endian words; only the ones read here */
#define HB_CTL_OFF              0x07AFF000UL
#define HB_MAGIC_READY          0x48425244UL	/* "HBRD" */
#define HB_W_MAGIC              0
#define HB_W_STATE              1
#define HB_W_FRAMES             6
#define HB_W_WIDTH              7
#define HB_W_HEIGHT             8
#define HB_W_TICKS_LO           9
#define HB_W_TICKS_HI           10
#define HB_W_CPU_HZ             15
#define HB_W_TICK_HZ            16
#define HB_W_HEARTBEAT          17
#define HB_W_PROGRESS           18
#define HB_W_FAULT              20
#define HB_W_FAULT_PC           22
#define HB_W_LOAD               23
#define HB_W_NAME               25	/* 16 bytes: the program's name (0: an older h264bench) */

static int fd;
static volatile unsigned long *hb;	/* h264bench's block, or 0 */
static unsigned long hb_beat;

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
core1_map()
{
    caddr_t p = mmap((caddr_t)0, 4096, PROT_READ, MAP_SHARED, fd,
        (off_t)(HB_CTL_OFF - ZZ9K_Z3660_FB_PHYS));

    if (p != (caddr_t)-1)
        hb = (volatile unsigned long *)p;
}

/* the core 1 program's name, as it put it into the block */
static char *
core1_name()
{
    static char name[17];
    int i;

    memcpy(name, (char *)&hb[HB_W_NAME], 16);
    name[16] = 0;
    for (i = 0; name[i]; i++)
        if (name[i] < ' ' || name[i] > '~')
            name[i] = 0;
    return name[0] ? name : "h264bench";
}

/* core 1, one line (loop 0) or a short column */
static void
core1(loop)
int loop;
{
    static char *st[] = { "idle", "job posted", "decoding", "done", "error" };
    unsigned long beat;

    if (reg32(REG_ZZ_EMULATION_USED)) {
        printf(loop ? "  core1 emu" : "ARM core 1      68040 emulator\n");
        return;
    }
    if (!hb || hb[HB_W_MAGIC] != HB_MAGIC_READY) {
        if (loop)
            printf("  core1 -");
        else
            printf("ARM core 1      nothing known runs there%s\n",
                hb ? "" : " (board memory not mappable)");
        return;
    }
    beat = hb[HB_W_HEARTBEAT];
    if (hb[HB_W_FAULT]) {
        if (loop)
            printf("  core1 FAULT %lu", hb[HB_W_FAULT]);
        else
            printf("ARM core 1      %s STOPPED: exception %lu at pc %08lx\n",
                core1_name(), hb[HB_W_FAULT], hb[HB_W_FAULT_PC]);
        return;
    }
    if (loop) {
        printf("  core1 %3lu%% %s", hb[HB_W_LOAD] / 10,
            hb[HB_W_STATE] == 2 ? "dec" : beat != hb_beat ? "idle" : "STUCK?");
        if (hb[HB_W_STATE] == 2)
            printf(" %lu fr", hb[HB_W_PROGRESS]);
    } else {
        printf("ARM core 1      %s, %s, load %lu.%lu %%, %lu MHz", core1_name(),
            hb[HB_W_STATE] <= 4 ? st[hb[HB_W_STATE]] : "?",
            hb[HB_W_LOAD] / 10, hb[HB_W_LOAD] % 10, hb[HB_W_CPU_HZ] / 1000000);
        if (hb[HB_W_STATE] == 2)
            printf(", %lu frames", hb[HB_W_PROGRESS]);
        printf("\n");
        if (hb[HB_W_FRAMES] && hb[HB_W_TICK_HZ]) {
            double sec = (hb[HB_W_TICKS_HI] * 4294967296.0 + hb[HB_W_TICKS_LO]) / hb[HB_W_TICK_HZ];

            printf("                last job %lu frames %lux%lu, %.1f fps\n", hb[HB_W_FRAMES],
                hb[HB_W_WIDTH], hb[HB_W_HEIGHT], sec > 0 ? hb[HB_W_FRAMES] / sec : 0.0);
        }
    }
    hb_beat = beat;
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
        core1(0);
    } else {
        printf("Zynq %5.1f C  060 %5.1f C  board %5.1f C  VCCINT %4.2f  3V3 %4.2f  5V %4.2f  %ld MHz",
            reg32(REG_ZZ_TEMPERATURE) * 0.1, reg32(REG_ZZ_LTC_060_TEMP) * 0.01,
            reg32(REG_ZZ_LTC_TEMP) * 0.01, reg32(REG_ZZ_VOLTAGE_INT) * 0.01,
            reg32(REG_ZZ_LTC_V1) * 0.01, reg32(REG_ZZ_LTC_V2) * 0.01, reg32(REG_ZZ_CPU_FREQ));
        core1(1);
        printf("\n");
    }
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
    core1_map();
    show(0);
    while (loop) {
        sleep(loop);
        show(1);
    }
    return 0;
}
