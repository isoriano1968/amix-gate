/*
 * zz9k_test.c - AMIX userspace diagnostics/smoke test for /dev/zz9000.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "../zz9000.h"

extern int getrlimit(), setrlimit();

static void draw_bars();
static void draw_bars32();
static int write_bars();
static void print_z2_expected();
static int print_autocon();
static void print_regs();
static void print_regs_z3660();
static unsigned short test_mode_id();
static int parse_resolution();
static int streq();
static void zz9k_hold();

static void
print_z2_expected()
{
    printf("Expected ZZ9000 Zorro II config for AMIX:\n");
    printf("  manufacturer  : 0x%04x\n", ZZ9K_MNT_MANUFACTURER);
    printf("  product       : 0x%02x (Zorro II)\n", ZZ9K_PRODUCT_Z2);
    printf("  unsupported   : product 0x%02x (Zorro III)\n", ZZ9K_PRODUCT_Z3);
    printf("  register base : board base + 0x00000000\n");
    printf("  framebuffer   : board base + 0x%08lx\n", ZZ9K_FB_OFFSET);
    printf("  Z2 fb size    : board size - 0x%08lx\n",
        ZZ9K_FB_OFFSET + ZZ9K_Z2_RESERVED_TAIL);
    printf("\n");
}

static unsigned short
test_mode_id(width, height)
unsigned short width;
unsigned short height;
{
    if (width == 1280 && height == 720) return ZZ9K_MODE_1280x720;
    if (width == 800 && height == 600) return ZZ9K_MODE_800x600;
    if (width == 640 && height == 480) return ZZ9K_MODE_640x480;
    if (width == 1024 && height == 768) return ZZ9K_MODE_1024x768;
    if (width == 1280 && height == 1024) return ZZ9K_MODE_1280x1024;
    if (width == 1920 && height == 1080) return ZZ9K_MODE_1920x1080;
    if (width == 720 && height == 576) return ZZ9K_MODE_720x576;
    if (width == 720 && height == 480) return ZZ9K_MODE_720x480;
    if (width == 640 && height == 512) return ZZ9K_MODE_640x512;
    if (width == 1600 && height == 1200) return ZZ9K_MODE_1600x1200;
    if (width == 2560 && height == 1440) return ZZ9K_MODE_2560x1440;
    if (width == 640 && height == 400) return ZZ9K_MODE_640x400;
    if (width == 1920 && height == 800) return ZZ9K_MODE_1920x800;
    if (width == 1280 && height == 800) return ZZ9K_Z3660_MODE_1280x800;
    if (width == 1920 && height == 1200) return ZZ9K_Z3660_MODE_1920x1200;
    return 0xffff;
}

static int
parse_resolution(s, width, height)
char *s;
unsigned short *width;
unsigned short *height;
{
    if (s[0] == '1' && s[1] == '2' && s[2] == '8' && s[3] == '0' &&
        s[4] == 'x' && s[5] == '7' && s[6] == '2' && s[7] == '0' &&
        s[8] == 0) {
        *width = 1280;
        *height = 720;
        return 1;
    }
    if (s[0] == '1' && s[1] == '2' && s[2] == '8' && s[3] == '0' &&
        s[4] == 'x' && s[5] == '1' && s[6] == '0' && s[7] == '2' &&
        s[8] == '4' && s[9] == 0) {
        *width = 1280;
        *height = 1024;
        return 1;
    }
    if (s[0] == '1' && s[1] == '6' && s[2] == '0' && s[3] == '0' &&
        s[4] == 'x' && s[5] == '1' && s[6] == '2' && s[7] == '0' &&
        s[8] == '0' && s[9] == 0) {
        *width = 1600;
        *height = 1200;
        return 1;
    }
    if (s[0] == '1' && s[1] == '9' && s[2] == '2' && s[3] == '0' &&
        s[4] == 'x' && s[5] == '8' && s[6] == '0' && s[7] == '0' &&
        s[8] == 0) {
        *width = 1920;
        *height = 800;
        return 1;
    }
    if (s[0] == '1' && s[1] == '9' && s[2] == '2' && s[3] == '0' &&
        s[4] == 'x' && s[5] == '1' && s[6] == '0' && s[7] == '8' &&
        s[8] == '0' && s[9] == 0) {
        *width = 1920;
        *height = 1080;
        return 1;
    }
    if (s[0] == '1' && s[1] == '2' && s[2] == '8' && s[3] == '0' &&
        s[4] == 'x' && s[5] == '8' && s[6] == '0' && s[7] == '0' &&
        s[8] == 0) {
        *width = 1280;
        *height = 800;
        return 1;
    }
    if (s[0] == '1' && s[1] == '9' && s[2] == '2' && s[3] == '0' &&
        s[4] == 'x' && s[5] == '1' && s[6] == '2' && s[7] == '0' &&
        s[8] == '0' && s[9] == 0) {
        *width = 1920;
        *height = 1200;
        return 1;
    }
    if (s[0] == '2' && s[1] == '5' && s[2] == '6' && s[3] == '0' &&
        s[4] == 'x' && s[5] == '1' && s[6] == '4' && s[7] == '4' &&
        s[8] == '0' && s[9] == 0) {
        *width = 2560;
        *height = 1440;
        return 1;
    }
    if (s[0] == '1' && s[1] == '0' && s[2] == '2' && s[3] == '4' &&
        s[4] == 'x' && s[5] == '7' && s[6] == '6' && s[7] == '8' &&
        s[8] == 0) {
        *width = 1024;
        *height = 768;
        return 1;
    }
    if (s[0] == '8' && s[1] == '0' && s[2] == '0' && s[3] == 'x' &&
        s[4] == '6' && s[5] == '0' && s[6] == '0' && s[7] == 0) {
        *width = 800;
        *height = 600;
        return 1;
    }
    if (s[0] == '6' && s[1] == '4' && s[2] == '0' && s[3] == 'x' &&
        s[4] == '4' && s[5] == '8' && s[6] == '0' && s[7] == 0) {
        *width = 640;
        *height = 480;
        return 1;
    }
    return 0;
}

static int
streq(a, b)
char *a;
char *b;
{
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

/*
 * Hold the picture on screen.  Interactive by default (a newline restores),
 * but ZZ9K_HOLD=<seconds> makes it non-interactive: hold that many seconds
 * and return, 0 to return at once.  Anything driven from a script or over a
 * connection whose stdin never reaches end of file wants ZZ9K_HOLD.
 */
static void
zz9k_hold(what)
char *what;
{
    char *v;
    int secs;

    v = (char *)getenv("ZZ9K_HOLD");
    if (v) {
        secs = atoi(v);
        printf("%s holding %d s (ZZ9K_HOLD)\n", what, secs);
        if (secs > 0)
            sleep(secs);
        return;
    }
    printf("%s Press return to restore capture: ", what);
    (void)getchar();
}

static int
getreg(fd, off, value)
int fd;
unsigned short off;
unsigned short *value;
{
    struct zz9k_reg zr;

    zr.offset = off;
    zr.value = 0;
    if (ioctl(fd, ZZ9KIOC_GETREG, &zr) < 0)
        return -1;
    *value = zr.value;
    return 0;
}

static void
print_regs(fd)
int fd;
{
    unsigned short v;

    printf("Safe ZZ9000 register reads:\n");
    if (getreg(fd, ZZ9K_REG_HW_VERSION, &v) == 0)
        printf("  HW_VERSION    @0x%04x = 0x%04x\n", ZZ9K_REG_HW_VERSION, v);
    else
        perror("  HW_VERSION");
    if (getreg(fd, ZZ9K_REG_FW_VERSION, &v) == 0)
        printf("  FW_VERSION    @0x%04x = 0x%04x\n", ZZ9K_REG_FW_VERSION, v);
    else
        perror("  FW_VERSION");
    if (getreg(fd, ZZ9K_REG_MODE, &v) == 0)
        printf("  MODE          @0x%04x = 0x%04x\n", ZZ9K_REG_MODE, v);
    else
        perror("  MODE");
    if (getreg(fd, ZZ9K_REG_CONFIG, &v) == 0)
        printf("  CONFIG        @0x%04x = 0x%04x\n", ZZ9K_REG_CONFIG, v);
    else
        perror("  CONFIG");
    if (getreg(fd, ZZ9K_REG_VCAP_MODE, &v) == 0)
        printf("  VCAP_MODE     @0x%04x = 0x%04x\n", ZZ9K_REG_VCAP_MODE, v);
    else
        perror("  VCAP_MODE");
    printf("\n");
}
/*
 * The Z3660 row (GETINFO zorro 3, product 1) has a 32-bit register file
 * from 0x100; GETREG returns the 16-bit lane at the given offset, and its
 * MODE register is write-only.
 */
static void
print_regs_z3660(fd)
int fd;
{
    unsigned short v;

    printf("Safe Z3660 register reads (32-bit registers, low lanes):\n");
    if (getreg(fd, ZZ9K_Z3660_REG_FW_VERSION + 2, &v) == 0)
        printf("  FW_VERSION    @0x%04x = 0x%04x\n",
            ZZ9K_Z3660_REG_FW_VERSION, v);
    else
        perror("  FW_VERSION");
    if (getreg(fd, ZZ9K_Z3660_REG_FW_BETA + 2, &v) == 0)
        printf("  FW_BETA       @0x%04x = 0x%04x\n", ZZ9K_Z3660_REG_FW_BETA, v);
    else
        perror("  FW_BETA");
    printf("  MODE is write-only on the Z3660; no readback\n");
    printf("\n");
}

static void
draw_bars(base, width, height)
char *base;
int width;
int height;
{
    unsigned short *fb;
    unsigned short color;
    int x, y;

    fb = (unsigned short *)base;
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            if (y < height / 2) {
                if (y < height / 6)
                    color = 0x0000;       /* Germany black */
                else if (y < (height * 2) / 6)
                    color = 0xf800;       /* Germany red */
                else
                    color = 0xffe0;       /* Germany gold */
            } else {
                if (y < height / 2 + height / 8)
                    color = 0xf800;       /* Spain red */
                else if (y < height - height / 8)
                    color = 0xffe0;       /* Spain yellow */
                else
                    color = 0xf800;       /* Spain red */
            }
            fb[(y * width) + x] = color;
        }
    }
}

static void
draw_bars32(base, width, height)
char *base;
int width;
int height;
{
    unsigned long *fb;
    unsigned long color;
    int x, y;

    fb = (unsigned long *)base;
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            if (y < height / 2) {
                if (y < height / 6)
                    color = 0x000000ff;   /* Germany black */
                else if (y < (height * 2) / 6)
                    color = 0x0000ddff;   /* Germany red */
                else
                    color = 0x00ccffff;   /* Germany gold */
            } else {
                if (y < height / 2 + height / 8)
                    color = 0x0000ddff;   /* Spain red */
                else if (y < height - height / 8)
                    color = 0x00c4ffff;   /* Spain yellow */
                else
                    color = 0x0000ddff;   /* Spain red */
            }
            fb[(y * width) + x] = color;
        }
    }
}

/*
 * The bars through write(2) instead of the mapping: the kernel copies each
 * row into the framebuffer from its own address space.  For a kernel whose
 * user device mappings are unproven (see zz9k_maptest).
 */
static int
write_bars(fd, fb_offset, width, height)
int fd;
unsigned long fb_offset;
int width, height;
{
    static unsigned short row[2560];
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
            row[x] = color;
        if (lseek(fd, (long)(fb_offset + (unsigned long)y * width * 2), 0) == -1L)
            return -1;
        if (write(fd, (char *)row, width * 2) != width * 2)
            return -1;
    }
    return 0;
}

static int
print_autocon(fd)
int fd;
{
    int i;
    int found;
    struct zz9k_autocon_dump dump;
    struct zz9k_autocon_entry *e;

    if (ioctl(fd, ZZ9KIOC_GETAUTOCON, &dump) < 0) {
        perror("ZZ9KIOC_GETAUTOCON");
        return 0;
    }

    found = 0;
    printf("AMIX autocon table (%u entries):\n", dump.count);
    printf("idx manf prod type flags base       size       slot slots\n");
    for (i = 0; i < dump.count; ++i) {
        e = &dump.entry[i];
        if (e->manufacturer == 0 && e->product == 0 &&
            e->board_addr == 0 && e->board_size == 0)
            continue;
        printf("%2d  %04x %02x   %02x   %02x    %08lx  %08lx  %04x %04x",
            i, e->manufacturer, e->product, e->type, e->flags,
            e->board_addr, e->board_size, e->slot_addr, e->slot_size);
        if (e->manufacturer == ZZ9K_MNT_MANUFACTURER)
            printf("  MNT");
        if (e->manufacturer == ZZ9K_MNT_MANUFACTURER &&
            e->product == ZZ9K_PRODUCT_Z2) {
            found |= 1;
            printf(" ZZ9000-Z2");
        }
        if (e->manufacturer == ZZ9K_MNT_MANUFACTURER &&
            e->product == ZZ9K_PRODUCT_Z3) {
            found |= 2;
            printf(" ZZ9000-Z3 unsupported-on-AMIX");
        }
        printf("\n");
    }
    return found;
}
int
main(argc, argv)
int argc;
char **argv;
{
    char *devname;
    char *board;
    char *fb;
    int fd;
    int rc;
    int found;
    struct zz9k_info info;
    struct zz9k_fbinfo fbi;
    struct zz9k_mode mode;
    struct zz9k_fill fill;
    struct zz9k_fillrect fr;
    struct zz9k_copyrect cp;
    unsigned short mode_reg;
    unsigned short requested_mode;
    unsigned short mode_id;
    unsigned long test_bytes, need;
    unsigned short test_bpp;
    struct rlimit rl;
    int rwbars;

    devname = "/dev/zz9000";
    if (argc > 1)
        devname = argv[1];

    print_z2_expected();

    fd = open(devname, O_RDWR);
    if (fd < 0) {
        perror(devname);
        printf("Open failed. Check kernel cdevsw major 49 and /dev node.\n");
        return 1;
    }

    found = print_autocon(fd);
    if ((found & 1) == 0) {
        if (found & 2)
            printf("Only ZZ9000-Z3/product 4 is present. AMIX driver requires Z2/product 3.\n");
        else
            printf("No ZZ9000 autocon entry found.\n");
        printf("Asking the kernel driver anyway (it may know a board the table does not).\n");
    }

    rc = ioctl(fd, ZZ9KIOC_PROBE, 0);
    if (rc < 0) {
        perror("ZZ9KIOC_PROBE");
        printf("No supported board was found. No framebuffer test run.\n");
        printf("If the table shows ZZ9000-Z3/product 4, reconfigure the card for Zorro II.\n");
        close(fd);
        return 1;
    }

    rc = ioctl(fd, ZZ9KIOC_GETINFO, &info);
    if (rc < 0) {
        perror("ZZ9KIOC_GETINFO");
        close(fd);
        return 1;
    }

    printf("zz9000: base 0x%lx board 0x%lx fb 0x%lx+0x%lx hw 0x%x fw 0x%x mode %ux%u color %u\n",
        info.base, info.board_size, info.fb_offset, info.fb_size,
        info.hw_version, info.fw_version, info.width, info.height,
        info.color_mode);
    printf("zz9000: zorro %u product 0x%02x\n", info.zorro, info.product);
    rc = ioctl(fd, ZZ9KIOC_GETFBINFO, &fbi);
    if (rc == 0) {
        printf("fbinfo: %ux%u depth %u bpp %u stride %u offset 0x%lx size 0x%lx\n",
            fbi.width, fbi.height, fbi.depth, fbi.bytes_per_pixel,
            fbi.stride, fbi.fb_offset, fbi.fb_size);
        printf("fbinfo: masks r 0x%lx g 0x%lx b 0x%lx a 0x%lx shifts %u/%u/%u/%u\n",
            fbi.red_mask, fbi.green_mask, fbi.blue_mask, fbi.alpha_mask,
            fbi.red_shift, fbi.green_shift, fbi.blue_shift,
            fbi.alpha_shift);
    } else
        perror("ZZ9KIOC_GETFBINFO");

    if (info.zorro == 3 && info.product == 1)
        print_regs_z3660(fd);
    else
        print_regs(fd);

    if (argc > 2 && streq(argv[2], "console")) {
        rc = ioctl(fd, ZZ9KIOC_CONSOLE, ZZ9K_CONSOLE_ENABLE);
        if (rc < 0) {
            perror("ZZ9KIOC_CONSOLE enable");
            close(fd);
            return 1;
        }
        rc = ioctl(fd, ZZ9KIOC_CONSOLE, ZZ9K_CONSOLE_TEST);
        if (rc < 0)
            perror("ZZ9KIOC_CONSOLE test");
        zz9k_hold("ZZ9000 console enabled.");
        rc = ioctl(fd, ZZ9KIOC_CONSOLE, ZZ9K_CONSOLE_DISABLE);
        if (rc < 0)
            perror("ZZ9KIOC_CONSOLE disable");
        close(fd);
        return 0;
    }
    if (argc > 2 && streq(argv[2], "consoletest")) {
        rc = ioctl(fd, ZZ9KIOC_CONSOLE, ZZ9K_CONSOLE_TEST);
        if (rc < 0) {
            perror("ZZ9KIOC_CONSOLE test");
            close(fd);
            return 1;
        }
        close(fd);
        return 0;
    }
    if (argc > 2 && (streq(argv[2], "redirect") || streq(argv[2], "mirror"))) {
        rc = ioctl(fd, ZZ9KIOC_CONSOLE, ZZ9K_CONSOLE_REDIRECT);
        if (rc < 0) {
            perror("ZZ9KIOC_CONSOLE redirect");
            close(fd);
            return 1;
        }
        printf("ZZ9000 console redirect enabled. Native RGB drawing should stop.\n");
        printf("Run a command such as 'date' or 'ls -l /' to verify redirected output.\n");
        close(fd);
        return 0;
    }
    if (argc > 2 && streq(argv[2], "noconsole")) {
        rc = ioctl(fd, ZZ9KIOC_CONSOLE, ZZ9K_CONSOLE_DISABLE);
        if (rc < 0) {
            perror("ZZ9KIOC_CONSOLE disable");
            close(fd);
            return 1;
        }
        close(fd);
        return 0;
    }
    if (argc > 2 && streq(argv[2], "blit")) {
        mode.width = 1920;
        mode.height = 800;
        mode.color_mode = ZZ9K_COLOR_16BIT565;
        mode.scale_mode = 0;
        if (argc > 3 && !parse_resolution(argv[3], &mode.width, &mode.height)) {
            printf("Unsupported blit resolution '%s'. Try 1920x1080, 1920x800, 1600x1200, 1280x1024, 1280x800, 1280x720, 1024x768, 800x600, or 640x480.\n",
                argv[3]);
            close(fd);
            return 1;
        }
        rc = ioctl(fd, ZZ9KIOC_SETMODE, &mode);
        if (rc < 0) {
            perror("ZZ9KIOC_SETMODE");
            close(fd);
            return 1;
        }
        fill.offset = 0;
        fill.bytes = (unsigned long)mode.width * mode.height * 2;
        fill.value = 0;
        rc = ioctl(fd, ZZ9KIOC_FILL, &fill);
        if (rc < 0)
            perror("ZZ9KIOC_FILL");

        fr.x = 0; fr.y = 0; fr.width = mode.width; fr.height = mode.height;
        fr.color = 0x0010;
        rc = ioctl(fd, ZZ9KIOC_FILLRECT, &fr);
        if (rc < 0)
            perror("ZZ9KIOC_FILLRECT background");
        fr.x = 80; fr.y = 80; fr.width = mode.width / 3; fr.height = mode.height / 4;
        fr.color = 0xf800;
        rc = ioctl(fd, ZZ9KIOC_FILLRECT, &fr);
        if (rc < 0)
            perror("ZZ9KIOC_FILLRECT red");
        fr.x = 160; fr.y = 180; fr.width = mode.width / 3; fr.height = mode.height / 4;
        fr.color = 0xffe0;
        rc = ioctl(fd, ZZ9KIOC_FILLRECT, &fr);
        if (rc < 0)
            perror("ZZ9KIOC_FILLRECT yellow");
        fr.x = 240; fr.y = 280; fr.width = mode.width / 3; fr.height = mode.height / 4;
        fr.color = 0x07e0;
        rc = ioctl(fd, ZZ9KIOC_FILLRECT, &fr);
        if (rc < 0)
            perror("ZZ9KIOC_FILLRECT green");

        cp.src_x = 80; cp.src_y = 80;
        cp.dst_x = mode.width / 2; cp.dst_y = mode.height / 2;
        cp.width = mode.width / 3; cp.height = mode.height / 4;
        rc = ioctl(fd, ZZ9KIOC_COPYRECT, &cp);
        if (rc < 0)
            perror("ZZ9KIOC_COPYRECT");

        rc = ioctl(fd, ZZ9KIOC_SETSWITCH, ZZ9K_SWITCH_RTG);
        if (rc < 0)
            perror("ZZ9KIOC_SETSWITCH RTG");
        printf("ZZ9000 blitter test active at %ux%u.\n", mode.width, mode.height);
        zz9k_hold("ZZ9000 blitter test active.");
        rc = ioctl(fd, ZZ9KIOC_SETSWITCH, ZZ9K_SWITCH_CAPTURE);
        if (rc < 0)
            perror("ZZ9KIOC_SETSWITCH CAPTURE");
        close(fd);
        return 0;
    }

    mode.width = 800;
    mode.height = 600;
    mode.color_mode = ZZ9K_COLOR_16BIT565;
    if (argc > 2 && argv[2][0] == '3' && argv[2][1] == '2')
        mode.color_mode = ZZ9K_COLOR_32BIT;
    rwbars = (argc > 2 && streq(argv[2], "rw"));
    if (argc > 3 && !parse_resolution(argv[3], &mode.width, &mode.height)) {
        printf("Unsupported test resolution '%s'. Try 1920x1080, 1600x1200, 1920x800, 1280x1024, 1280x800, 1280x720, 1024x768, 800x600, or 640x480.\n",
            argv[3]);
        close(fd);
        return 1;
    }
    mode.scale_mode = 0;
    mode_id = test_mode_id(mode.width, mode.height);
    if (mode_id == 0xffff) {
        printf("No ZZ9000 mode id for %ux%u.\n", mode.width, mode.height);
        close(fd);
        return 1;
    }
    test_bpp = (mode.color_mode == ZZ9K_COLOR_32BIT) ? 4 : 2;
    test_bytes = (unsigned long)mode.width * mode.height * test_bpp;
    if (test_bytes > info.fb_size) {
        printf("Requested test needs 0x%lx bytes, but Z2 framebuffer has 0x%lx bytes.\n",
            test_bytes, info.fb_size);
        printf("At 32-bit, try 1280x720 or 1024x768. At 16-bit, try up to 1600x1200 or 1920x800.\n");
        close(fd);
        return 1;
    }
    requested_mode = (unsigned short)(mode_id |
        (mode.color_mode << 8) | (mode.scale_mode << 12));
    printf("Requesting MODE write = 0x%04x (%ux%u color %u)\n",
        requested_mode, mode.width, mode.height, mode.color_mode);
    rc = ioctl(fd, ZZ9KIOC_SETMODE, &mode);
    if (rc < 0) {
        perror("ZZ9KIOC_SETMODE");
        close(fd);
        return 1;
    }
    if (info.zorro == 3 && info.product == 1)
        printf("MODE readback skipped (write-only on the Z3660)\n");
    else if (getreg(fd, ZZ9K_REG_MODE, &mode_reg) == 0)
        printf("MODE readback after SETMODE = 0x%04x\n", mode_reg);
    rc = ioctl(fd, ZZ9KIOC_GETINFO, &info);
    if (rc == 0)
        printf("Driver mode after SETMODE: mode %u %ux%u color %u\n",
            info.mode, info.width, info.height, info.color_mode);

    if (rwbars) {
        if (write_bars(fd, info.fb_offset, mode.width, mode.height) < 0) {
            perror("write");
            close(fd);
            return 1;
        }
        rc = ioctl(fd, ZZ9KIOC_SETSWITCH, ZZ9K_SWITCH_RTG);
        if (rc < 0)
            perror("ZZ9KIOC_SETSWITCH RTG");
        zz9k_hold("RTG test pattern active (drawn through write(2), no mapping).");
        rc = ioctl(fd, ZZ9KIOC_SETSWITCH, ZZ9K_SWITCH_CAPTURE);
        if (rc < 0)
            perror("ZZ9KIOC_SETSWITCH CAPTURE");
        close(fd);
        return 0;
    }

    /*
     * The mapping counts against the process VM limit, whose default (16 MB)
     * is below a large framebuffer; raise the soft limit towards the hard one.
     */
    if (getrlimit(RLIMIT_VMEM, &rl) == 0) {
        need = info.board_size + 0x800000;
        if ((unsigned long)rl.rlim_cur < need) {
            rl.rlim_cur = need;
            if ((unsigned long)rl.rlim_max < need)
                rl.rlim_cur = rl.rlim_max;
            (void)setrlimit(RLIMIT_VMEM, &rl);
        }
    }
    board = (char *)mmap((caddr_t)0, info.board_size,
        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ((long)board == -1) {
        perror("mmap");
        printf("Cannot map 0x%lx bytes of board window. If errno is ENOMEM, raise the\n",
            info.board_size);
        printf("process VM limit first (e.g. ulimit -v 131072); the default is 16 MB.\n");
        rc = ioctl(fd, ZZ9KIOC_SETSWITCH, ZZ9K_SWITCH_CAPTURE);
        if (rc < 0)
            perror("ZZ9KIOC_SETSWITCH CAPTURE");
        close(fd);
        return 1;
    }

    fb = board + info.fb_offset;
    if (mode.color_mode == ZZ9K_COLOR_32BIT)
        draw_bars32(fb, mode.width, mode.height);
    else
        draw_bars(fb, mode.width, mode.height);

    rc = ioctl(fd, ZZ9KIOC_SETSWITCH, ZZ9K_SWITCH_RTG);
    if (rc < 0)
        perror("ZZ9KIOC_SETSWITCH RTG");

    zz9k_hold("RTG test pattern active.");

    rc = ioctl(fd, ZZ9KIOC_SETSWITCH, ZZ9K_SWITCH_CAPTURE);
    if (rc < 0)
        perror("ZZ9KIOC_SETSWITCH CAPTURE");

    (void)munmap(board, info.board_size);
    close(fd);
    return 0;
}
