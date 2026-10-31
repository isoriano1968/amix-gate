/*
 * zz9k_memprobe.c - probe the Z3660 RTG window through /dev/mem, no driver.
 *
 *   zz9k_memprobe [-q] [-r] [hold-seconds] [mode-id] [base-hex]
 *
 * Defaults: hold 30 s, mode 5 (1920x1080 at 60 Hz), base 0x10000000 (the
 * firmware's hard map with autoconfig_rtg NO).  Root only.
 *
 * -q: the signature only -- an lseek + one 4-byte read of FW_VERSION (and the
 *     two version registers) through /dev/mem, then exit: 0 if the board
 *     answers with the expected major, 1 if it does not.  Touches no mode and
 *     writes nothing, so it is safe to run at any time; it is the canary for
 *     "does the board answer at all", independent of the driver.  Note that
 *     `dd ... skip=N` is NOT a substitute: this box's dd reads its way to the
 *     skip point, so a skip to 0x10000000 issues tens of millions of reads
 *     and looks like a hang.
 *
 * -r: no mmap at all -- every access is an lseek + read/write on /dev/mem,
 *     done by the kernel from its identity map.  For a kernel whose /dev/mem
 *     mmap does not land on the requested physical address (the self-test
 *     below catches that).  Registers are written as single 4-byte
 *     transfers at aligned offsets; the framebuffer is written a row at a
 *     time.  The 16-bit lane reads are skipped in this mode (their width
 *     through the kernel's copy loop is not controllable).
 *
 * What it does, in order, announcing every access BEFORE making it (output
 * is unbuffered, SIGBUS/SIGSEGV are caught, so an unresponsive address is a
 * printed result, not a dead process):
 *   0  raise the process VM limit to its hard limit
 *   1  self-test: /dev/mem mmap of the kernel load base must agree with
 *      lseek+read of the same longword, else the mapping is not landing
 *   2  map the register page; 32-bit reads of FW_VERSION (0x1a0, expect
 *      0x00000103), FW_BETA (0x280), FW_ALFA (0x284), 0x000 and MODE
 *      (0x100, both expected 0xffffffff); 16-bit reads of 0x1a0/0x1a2
 *      (the lane question)
 *   3  map a framebuffer slice at +0x200000
 *   4  set the mode with the driver's own write sequence (32-bit stores)
 *   5  paint the zz9k_test bar pattern with 16-bit stores, store and read
 *      back a longword, and cross-check two rows by lseek+read (byte order)
 *   6  hold, fill blue, hold, paint the bars again, exit -- the Z3660 has
 *      no switch back, the last picture stays on its HDMI
 *
 * Only the register page and the framebuffer slice are mapped, never the
 * whole 128 MB window.  Cross-compiled for AMIX (K&R C).
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>

extern int getrlimit(), setrlimit();

#define PAGESZ      4096
#define KNOWNPHYS   0x08000000L     /* kernel load base: readable, stable */
#define REG_PAGE    0x1000L
#define FB_PHYS     0x200000L
#define FB_MAP      0x800000L       /* 8 MB: 1920x1200x2 fits */

#define R_MODE      0x100
#define R_X1        0x110
#define R_Y1        0x114
#define R_X2        0x118
#define R_Y2        0x11c
#define R_PAN       0x120
#define R_BLIT_SRC  0x140
#define R_BLIT_DST  0x144
#define R_COLORMODE 0x148
#define R_SPLIT_POS 0x188
#define R_FW        0x1a0
#define R_FW_BETA   0x280
#define R_FW_ALFA   0x284

static jmp_buf busjmp;
static int bushit;
static int bussig;
static char *step = "";

static void
onbus(sig)
int sig;
{
    bushit = 1;
    bussig = sig;
    longjmp(busjmp, 1);
}

static void
armfault()
{
    signal(SIGBUS, onbus);
    signal(SIGSEGV, onbus);
}

static void
fatal(what)
char *what;
{
    printf("PROBE FAULT sig=%d during %s (%s) -- stopping here\n", bussig, step, what);
    exit(1);
}

static unsigned long
rd32(p, what)
volatile unsigned long *p;
char *what;
{
    unsigned long v;

    printf("READ32  %s @%08lx ...\n", what, (unsigned long)p);
    armfault();
    bushit = 0;
    v = 0;
    if (setjmp(busjmp) == 0)
        v = *p;
    if (bushit)
        fatal(what);
    printf("READ32  %s = %08lx\n", what, v);
    return v;
}

static unsigned short
rd16(p, what)
volatile unsigned short *p;
char *what;
{
    unsigned short v;

    printf("READ16  %s @%08lx ...\n", what, (unsigned long)p);
    armfault();
    bushit = 0;
    v = 0;
    if (setjmp(busjmp) == 0)
        v = *p;
    if (bushit)
        fatal(what);
    printf("READ16  %s = %04x\n", what, v);
    return v;
}

static void
wr32(p, v, what)
volatile unsigned long *p;
unsigned long v;
char *what;
{
    printf("WRITE32 %s @%08lx = %08lx ...\n", what, (unsigned long)p, v);
    armfault();
    bushit = 0;
    if (setjmp(busjmp) == 0)
        *p = v;
    if (bushit)
        fatal(what);
}

static unsigned long
physread(fd, addr)
int fd;
unsigned long addr;
{
    unsigned long v;

    if (lseek(fd, (long)addr, 0) == -1L)
        return 0xdead0001L;
    if (read(fd, (char *)&v, 4) != 4)
        return 0xdead0002L;
    return v;
}

static int
physwrite(fd, addr, v)
int fd;
unsigned long addr;
unsigned long v;
{
    if (lseek(fd, (long)addr, 0) == -1L)
        return -1;
    if (write(fd, (char *)&v, 4) != 4)
        return -1;
    return 0;
}

/* register access through /dev/mem: one 4-byte transfer, announced */
static unsigned long
prd32(fd, addr, what)
int fd;
unsigned long addr;
char *what;
{
    unsigned long v;

    printf("READ32  %s @%08lx via /dev/mem ...\n", what, addr);
    v = physread(fd, addr);
    printf("READ32  %s = %08lx\n", what, v);
    return v;
}

static void
pwr32(fd, addr, v, what)
int fd;
unsigned long addr;
unsigned long v;
char *what;
{
    printf("WRITE32 %s @%08lx = %08lx via /dev/mem ...\n", what, addr, v);
    if (physwrite(fd, addr, v) < 0)
        printf("WRITE32 %s FAILED errno=%d\n", what, errno);
}

static unsigned short rowbuf[2560];

/* paint through /dev/mem, one row per write(2) */
static int
paint_rows(fd, fbphys, width, height, pattern, color)
int fd;
unsigned long fbphys;
int width, height, pattern;
unsigned short color;
{
    int x, y;
    unsigned short c;

    for (y = 0; y < height; ++y) {
        if (pattern) {
            if (y < height / 2) {
                if (y < height / 6) c = 0x0000;
                else if (y < (height * 2) / 6) c = 0xf800;
                else c = 0xffe0;
            } else {
                if (y < height / 2 + height / 8) c = 0xf800;
                else if (y < height - height / 8) c = 0xffe0;
                else c = 0xf800;
            }
        } else
            c = color;
        for (x = 0; x < width; ++x)
            rowbuf[x] = c;
        if (lseek(fd, (long)(fbphys + (unsigned long)y * width * 2), 0) == -1L)
            return -1;
        if (write(fd, (char *)rowbuf, width * 2) != width * 2)
            return -1;
    }
    return 0;
}

static void
bars(fb, width, height)
volatile unsigned short *fb;
int width, height;
{
    int x, y;
    unsigned short color;

    for (y = 0; y < height; ++y) {
        if (y < height / 2) {
            if (y < height / 6) color = 0x0000;
            else if (y < (height * 2) / 6) color = 0xf800;
            else color = 0xffe0;
        } else {
            if (y < height / 2 + height / 8) color = 0xf800;
            else if (y < height - height / 8) color = 0xffe0;
            else color = 0xf800;
        }
        for (x = 0; x < width; ++x)
            fb[(y * width) + x] = color;
    }
}

static void
fill(fb, width, height, color)
volatile unsigned short *fb;
int width, height;
unsigned short color;
{
    long n;

    n = (long)width * height;
    while (n--)
        *fb++ = color;
}

static unsigned long
hexval(s)
char *s;
{
    unsigned long v;
    char c;

    v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    while ((c = *s++) != 0) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
    }
    return v;
}

int
main(argc, argv)
int argc;
char **argv;
{
    int fd, mode, row, rwmode, qmode;
    volatile int hold, width, height;
    volatile unsigned long base;
    unsigned long v, viaread;
    char * volatile regs;
    char *fbm;
    volatile unsigned long *lp;
    volatile unsigned short *fb;
    struct rlimit rl;

    setbuf(stdout, (char *)0);
    rwmode = 0;
    qmode = 0;
    while (argc > 1 && argv[1][0] == '-') {
        if (argv[1][1] == 'r')
            rwmode = 1;
        else if (argv[1][1] == 'q') {
            qmode = 1;
            rwmode = 1;
        } else {
            printf("usage: zz9k_memprobe [-q] [-r] [hold] [mode] [base]\n");
            return 2;
        }
        --argc;
        ++argv;
    }
    hold = (argc > 1) ? atoi(argv[1]) : 30;
    mode = (argc > 2) ? atoi(argv[2]) : 5;
    base = (argc > 3) ? hexval(argv[3]) : 0x10000000L;
    switch (mode) {
    case 0: width = 1280; height = 720; break;
    case 1: width = 800; height = 600; break;
    case 5: width = 1920; height = 1080; break;
    case 17: width = 1280; height = 800; break;
    case 18: width = 1920; height = 1200; break;
    default:
        printf("mode %d: not in this tool's table (0,1,5,17,18)\n", mode);
        return 2;
    }
    printf("zz9k_memprobe: base %08lx mode %d (%dx%d 565) hold %d s%s\n",
        base, mode, width, height, hold, rwmode ? " -- /dev/mem read/write, no mmap" : "");

    step = "rlimit";
    if (getrlimit(RLIMIT_VMEM, &rl) == 0) {
        printf("RLIMIT_VMEM cur %ld max %ld\n", (long)rl.rlim_cur, (long)rl.rlim_max);
        rl.rlim_cur = rl.rlim_max;
        if (setrlimit(RLIMIT_VMEM, &rl) < 0)
            printf("setrlimit failed errno=%d (continuing, maps are small)\n", errno);
    }

    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) {
        printf("cannot open /dev/mem errno=%d\n", errno);
        return 3;
    }

    if (rwmode) {
        unsigned long fbphys, marker;

        /* 1. the signature: the same 4-byte read the driver makes first;
         *    this page is already read by the ethernet driver on this
         *    kernel, so the address is known to respond */
        step = "signature";
        v = prd32(fd, base + R_FW, "FW_VERSION 0x1a0");
        printf("FW_VERSION %s (expect 00000103)\n",
            ((v >> 8) == 1) ? "PRESENT" : "NOT the expected signature");
        prd32(fd, base + R_FW_BETA, "FW_BETA 0x280");
        prd32(fd, base + R_FW_ALFA, "FW_ALFA 0x284");
        if (qmode) {
            close(fd);
            printf("zz9k_memprobe: the board %s at %08lx\n",
                ((v >> 8) == 1) ? "ANSWERS" : "does NOT answer", base);
            return ((v >> 8) == 1) ? 0 : 1;
        }
        prd32(fd, base + 0, "offset 0x000 (expect ffffffff)");
        prd32(fd, base + R_MODE, "MODE 0x100 (write-only, expect ffffffff)");
        if ((v >> 8) != 1) {
            printf("no Z3660 RTG signature at %08lx -- not touching the mode\n", base);
            return 1;
        }
        /* 2. self-test of the write path on the card's own RAM: the last
         *    row of the framebuffer slice, read back */
        step = "framebuffer selftest";
        fbphys = base + FB_PHYS;
        marker = fbphys + (unsigned long)width * 2 * (height - 1);
        pwr32(fd, marker, 0x001f001fL, "longword store at the last row");
        v = prd32(fd, marker, "longword readback");
        printf("framebuffer write/read via /dev/mem %s\n",
            (v == 0x001f001fL) ? "MATCH" : "MISMATCH -- stopping");
        if (v != 0x001f001fL)
            return 1;
        /* 3. the driver's mode-set sequence, one 4-byte write each */
        step = "mode set";
        pwr32(fd, base + R_X1, 0L, "X1");
        pwr32(fd, base + R_Y1, 0L, "Y1");
        pwr32(fd, base + R_X2, 0L, "X2");
        pwr32(fd, base + R_Y2, 0L, "Y2");
        pwr32(fd, base + R_COLORMODE, 1L, "COLORMODE=565");
        pwr32(fd, base + R_BLIT_SRC, 0L, "BLIT_SRC");
        pwr32(fd, base + R_BLIT_DST, 0L, "BLIT_DST");
        pwr32(fd, base + R_SPLIT_POS, 0L, "SPLIT_POS");
        pwr32(fd, base + R_PAN, 0L, "PAN");
        pwr32(fd, base + R_MODE, (unsigned long)(mode | (1 << 8)), "MODE");
        printf("MODE written: %dx%d 565 -- the HDMI should now show this mode\n",
            width, height);
        /* 4. paint, a row per write(2) */
        step = "paint";
        printf("painting bars via /dev/mem writes ...\n");
        if (paint_rows(fd, fbphys, width, height, 1, 0) < 0) {
            printf("paint failed errno=%d\n", errno);
            return 1;
        }
        row = height / 6 + 2;
        viaread = physread(fd, fbphys + (unsigned long)row * width * 2);
        printf("row %d via lseek+read = %08lx (expect f800f800)\n", row, viaread);
        viaread = physread(fd, fbphys);
        printf("row 0 via lseek+read = %08lx (expect 00000000)\n", viaread);
        printf("FRAME 1 NOW: bars %dx%d -- holding %d s\n", width, height, hold);
        sleep(hold);
        step = "fill";
        if (paint_rows(fd, fbphys, width, height, 0, 0x001f) < 0) {
            printf("fill failed errno=%d\n", errno);
            return 1;
        }
        printf("FRAME 2 NOW: blue -- holding %d s\n", hold);
        sleep(hold);
        if (paint_rows(fd, fbphys, width, height, 1, 0) < 0) {
            printf("bars again failed errno=%d\n", errno);
            return 1;
        }
        printf("FRAME 3 NOW: bars again -- leaving them on the HDMI (no switch back)\n");
        close(fd);
        printf("zz9k_memprobe: done\n");
        return 0;
    }

    step = "selftest";
    regs = (char *)mmap((caddr_t)0, PAGESZ, PROT_READ, MAP_SHARED, fd, KNOWNPHYS);
    if ((long)regs == -1) {
        printf("SELFTEST mmap(%08lx) failed errno=%d\n", KNOWNPHYS, errno);
        return 3;
    }
    v = rd32((volatile unsigned long *)regs, "selftest via mmap");
    viaread = physread(fd, KNOWNPHYS);
    printf("SELFTEST phys %08lx: mmap %08lx lseek+read %08lx %s\n", KNOWNPHYS, v,
        viaread, (v == viaread) ? "AGREE" : "DISAGREE -- mapping unusable");
    munmap((caddr_t)regs, PAGESZ);
    if (v != viaread)
        return 3;

    step = "register page";
    regs = (char *)mmap((caddr_t)0, REG_PAGE, PROT_READ | PROT_WRITE, MAP_SHARED,
        fd, (long)base);
    if ((long)regs == -1) {
        printf("mmap(%08lx) failed errno=%d\n", base, errno);
        return 3;
    }
    printf("register page %08lx mapped at %08lx\n", base, (unsigned long)regs);
    v = rd32((volatile unsigned long *)(regs + R_FW), "FW_VERSION 0x1a0");
    printf("FW_VERSION %s (expect 00000103, major %ld minor %ld)\n",
        ((v >> 8) == 1) ? "PRESENT" : "NOT the expected signature",
        (v >> 8) & 0xff, v & 0xff);
    rd32((volatile unsigned long *)(regs + R_FW_BETA), "FW_BETA 0x280");
    rd32((volatile unsigned long *)(regs + R_FW_ALFA), "FW_ALFA 0x284");
    rd32((volatile unsigned long *)(regs + 0), "offset 0x000 (expect ffffffff)");
    rd32((volatile unsigned long *)(regs + R_MODE), "MODE 0x100 (write-only, expect ffffffff)");
    rd16((volatile unsigned short *)(regs + R_FW), "FW_VERSION high lane 0x1a0 (expect 0000)");
    rd16((volatile unsigned short *)(regs + R_FW + 2), "FW_VERSION low lane 0x1a2 (expect 0103)");
    if ((v >> 8) != 1) {
        printf("no Z3660 RTG signature at %08lx -- not touching the mode\n", base);
        return 1;
    }

    step = "framebuffer map";
    fbm = (char *)mmap((caddr_t)0, FB_MAP, PROT_READ | PROT_WRITE, MAP_SHARED,
        fd, (long)(base + FB_PHYS));
    if ((long)fbm == -1) {
        printf("mmap(%08lx) failed errno=%d\n", base + FB_PHYS, errno);
        return 3;
    }
    printf("framebuffer %08lx (%ld bytes) mapped at %08lx\n", base + FB_PHYS,
        (long)FB_MAP, (unsigned long)fbm);
    fb = (volatile unsigned short *)fbm;

    step = "mode set";
    wr32((volatile unsigned long *)(regs + R_X1), 0L, "X1");
    wr32((volatile unsigned long *)(regs + R_Y1), 0L, "Y1");
    wr32((volatile unsigned long *)(regs + R_X2), 0L, "X2");
    wr32((volatile unsigned long *)(regs + R_Y2), 0L, "Y2");
    wr32((volatile unsigned long *)(regs + R_COLORMODE), 1L, "COLORMODE=565");
    wr32((volatile unsigned long *)(regs + R_BLIT_SRC), 0L, "BLIT_SRC");
    wr32((volatile unsigned long *)(regs + R_BLIT_DST), 0L, "BLIT_DST");
    wr32((volatile unsigned long *)(regs + R_SPLIT_POS), 0L, "SPLIT_POS");
    wr32((volatile unsigned long *)(regs + R_PAN), 0L, "PAN");
    wr32((volatile unsigned long *)(regs + R_MODE),
        (unsigned long)(mode | (1 << 8)), "MODE");
    printf("MODE written: %dx%d 565 -- the HDMI should now show this mode\n", width, height);

    step = "paint";
    printf("painting bars (16-bit stores) ...\n");
    armfault();
    bushit = 0;
    if (setjmp(busjmp) == 0)
        bars(fb, width, height);
    if (bushit)
        fatal("16-bit framebuffer stores");
    lp = (volatile unsigned long *)(fbm + (long)width * 2 * (height - 1));
    wr32(lp, 0x001f001fL, "longword store at the last row (two blue pixels)");
    v = rd32(lp, "longword readback");
    printf("longword store/readback %s\n", (v == 0x001f001fL) ? "MATCH" : "MISMATCH");
    row = height / 6 + 2;            /* inside the red band */
    viaread = physread(fd, base + FB_PHYS + (long)row * width * 2);
    printf("row %d via lseek+read = %08lx (expect f800f800: byte order of 16-bit stores)\n",
        row, viaread);
    viaread = physread(fd, base + FB_PHYS);
    printf("row 0 via lseek+read = %08lx (expect 00000000)\n", viaread);

    printf("FRAME 1 NOW: bars %dx%d -- holding %d s\n", width, height, hold);
    sleep(hold);
    step = "fill";
    armfault();
    bushit = 0;
    if (setjmp(busjmp) == 0)
        fill(fb, width, height, 0x001f);
    if (bushit)
        fatal("fill");
    printf("FRAME 2 NOW: blue -- holding %d s\n", hold);
    sleep(hold);
    armfault();
    bushit = 0;
    if (setjmp(busjmp) == 0)
        bars(fb, width, height);
    if (bushit)
        fatal("bars again");
    printf("FRAME 3 NOW: bars again -- leaving them on the HDMI (no switch back)\n");
    munmap((caddr_t)fbm, FB_MAP);
    munmap((caddr_t)regs, REG_PAGE);
    close(fd);
    printf("zz9k_memprobe: done\n");
    return 0;
}
