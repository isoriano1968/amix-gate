/*
 * rtgfb.c -- generic RTG framebuffer driver for AMIX: /dev/rtgfb, major 54.
 *
 * The kernel side of graphics cards whose mode setting and drawing run in
 * the X server's card module (the "rtgfb" design of amix-userland
 * components/x11/xzz9000/docs/MODULAR-X-SERVER-EVALUATION.md, section 5).
 * The driver only
 *   - finds the board's register and memory boards in the AutoConfig table
 *     (autocon(), filled by the boot loader from Kickstart's ConfigDev list),
 *   - tells user space what it found (RTGFBIOC_GETINFO),
 *   - maps the two boards for mmap(2) (d_mmap), and
 *   - switches the monitor between the Amiga video and the card
 *     (SVGAIOCSet/GetMonitorSwitch, and back to the Amiga video on the last
 *     close, so a server that dies leaves a usable picture).
 * It looks for its board once, at boot (rtgfbinit, called after parinit by
 * the kernel's driver chain amixdrv_init, see port/src/amixdrv_init.s.in and
 * patch_io_init.py), and names the board it finds on the console; without a
 * board it says nothing, and every open is a cheap ENXIO.  A kernel linked
 * without the chain looks at the first open.
 *
 * Boards (rtgfb_boardtab): the Village Tronic Picasso II and II+ (Cirrus
 * Logic GD5426/GD5428, Zorro II, 1 or 2 MB).  Their AutoConfig numbers are
 * manufacturer 2167, memory board product 11, register board product 12
 * (NetBSD sys/arch/amiga/dev/grf_cl.c grfclmatch; FS-UAE 3.2.35 gfxboard.cpp
 * BOARD_MANUFACTURER_PICASSO, BOARD_MODEL_MEMORY_PICASSOII,
 * BOARD_MODEL_REGISTERS_PICASSOII).
 *
 * Cache class.  The register board stays an ordinary unmanaged device page,
 * which the 68040/68060 port kernels map noncacheable-serialised: the VGA
 * index/data pairs must reach the chip in order.  The driver's own register
 * accesses (monitor switch, chip id) go, on those kernels, through a kernel
 * mapping of the register board made once at attach with dev_kvmap()
 * noncacheable-serialised (-DRTGFB_KVA, the kit-040 build; dev_kvmap comes
 * from the kit's devkvmap040.o or from a kernel that has it), not through
 * the identity-mapped low 1 GB, which is noncacheable but not serialised.
 * The framebuffer is written by the CPU and by the card's BitBLT engine behind the CPU's back, so it
 * must not be cached either; on port kernels that have the cache-class
 * selector (hat_cm_fb_add, built with -DRTGFB_FB_NC) it is registered
 * noncacheable-not-serialised, so that the 68060's store buffer can drain
 * framebuffer writes without stalling on each one.  The stock 68030 kernel
 * writes device PTEs without the CI bit; there the 68030's data cache is
 * write-through and the Zorro II bus is not cached by the A3000/A2000
 * (the 030's CIIN), so the same mapping is correct there too.
 *
 * Kernel C as AMIX's own drivers: K&R, no switch statements (the port's
 * 68060 relink gate reads jump tables as code), page numbers only through
 * phystopfn() so the one source fits 2 KB (stock) and 4 KB (Model-B) pages.
 */

#include "sys/types.h"
#include "sys/param.h"
#include "sys/immu.h"
#include "sys/sysmacros.h"
#include "sys/errno.h"
#include "sys/systm.h"
#include "rtgfb.h"

extern int autocon();
#ifdef RTGFB_FB_NC
extern int hat_cm_fb_add();
#endif
#ifdef RTGFB_KVA
/* port/src/devkvmap040.s: an MMIO window in kernel VA with an explicit
 * cache class (0x40 noncacheable-serialised); flags bit 0 = NOSLEEP */
extern char *dev_kvmap();
#define RTGFB_CM_NCS    0x40
#define RTGFB_REGS(bp)  ((bp)->reg_kva)
#else
#define RTGFB_REGS(bp)  ((bp)->reg_phys)
#endif

#define RTGFB_MAXBOARDS 1

/* the Zorro II space the kernel reaches 1:1 (Zorro III boards: not yet) */
#define RTGFB_Z2_LO     0x00200000UL
#define RTGFB_Z2_HI     0x00f00000UL

/* Picasso II monitor switch: a byte write to the register board.  Even
 * offsets; the value is ignored by the board (NetBSD writes 0xff / 0x01). */
#define RTGFB_P2_SW_CARD   0x8000       /* the card's picture (NetBSD RegOffpass) */
#define RTGFB_P2_SW_AMIGA  0x9000       /* Amiga video through (RegOnpass)        */

struct rtgfb_boardtab {
	char           *name;
	unsigned short  manufacturer;
	unsigned char   reg_product;
	unsigned char   mem_product;
	unsigned long   reg_min;        /* the register window the board needs */
	unsigned long   mem_min;        /* the least memory board it can be    */
	unsigned long   board;          /* RTGFB_BOARD_*                       */
	unsigned long   flags;          /* RTGFB_F_*                           */
};

/* Picasso II: the chip on the board (CR27 >> 2) names the model */
#define RTGFB_CL_GD5426 0x24            /* Picasso II  */
#define RTGFB_CL_GD5428 0x26            /* Picasso II+ */

static struct rtgfb_boardtab rtgfb_boardtab[] = {
	{ "Picasso II", 2167, 12, 11, 0x10000, 0x100000,
	  RTGFB_BOARD_PICASSO2, RTGFB_F_MONSW },
};
#define RTGFB_NBOARDTAB (sizeof rtgfb_boardtab / sizeof rtgfb_boardtab[0])

struct rtgfb_board {
	struct rtgfb_boardtab *bt;      /* NULL: not attached (yet)            */
	unsigned long   reg_phys;
	unsigned long   reg_size;
	unsigned long   mem_phys;
	unsigned long   mem_size;
	int             monitor;        /* SVGAMONITORSWITCH_*                 */
	int             looked;         /* the AutoConfig table was searched   */
	char           *name;           /* the model found                     */
#ifdef RTGFB_KVA
	unsigned long   reg_kva;        /* the register board in kernel VA     */
#endif
};

static struct rtgfb_board rtgfb_boards[RTGFB_MAXBOARDS];

/* a window of the AutoConfig table that the kernel and mmap may use */
static int
rtgfb_z2ok(base, size)
unsigned long base;
unsigned long size;
{
	return base >= RTGFB_Z2_LO && size > 0 && base < RTGFB_Z2_HI &&
	    RTGFB_Z2_HI - base >= size;
}

static void
rtgfb_switch(bp, which)
struct rtgfb_board *bp;
int which;
{
	volatile unsigned char *r;

	if (!bp->bt || !(bp->bt->flags & RTGFB_F_MONSW))
		return;
	r = (volatile unsigned char *)RTGFB_REGS(bp);
	if (bp->bt->board == RTGFB_BOARD_PICASSO2) {
		if (which == SVGAMONITORSWITCH_SVGA)
			r[RTGFB_P2_SW_CARD] = 0xff;
		else
			r[RTGFB_P2_SW_AMIGA] = 0x01;
	}
	bp->monitor = which;
}

/*
 * The Picasso II's model from its chip id (CR27 bits 7:2): the GD5426 is a
 * Picasso II, the GD5428 a II+.  Reads only, plus the CRTC index register,
 * at the CRTC address the chip uses now (Misc Output bit 0: 3Dx or 3Bx).
 */
static char *
rtgfb_p2model(bp)
struct rtgfb_board *bp;
{
	volatile unsigned char *r;
	int crt, id;

	r = (volatile unsigned char *)RTGFB_REGS(bp);
	crt = (r[0x3cc] & 1) ? 0x3d4 : 0x3b4;
	r[crt] = 0x27;
	id = r[crt + 1] >> 2;
	if (id == RTGFB_CL_GD5426)
		return "Picasso II (CL-GD5426)";
	if (id == RTGFB_CL_GD5428)
		return "Picasso II+ (CL-GD5428)";
	return bp->bt->name;
}

/*
 * Find board `unit` of the table: both of its AutoConfig entries, in the
 * Zorro II window, at least as large as the board needs.  Searched once
 * (at boot by rtgfbinit, else at the first open); the result stays.
 */
static int
rtgfb_attach(unit)
int unit;
{
	struct rtgfb_board *bp;
	struct rtgfb_boardtab *bt;
	long rb, rs, mb, ms;
	int i;

	if (unit < 0 || unit >= RTGFB_MAXBOARDS)
		return 0;
	bp = &rtgfb_boards[unit];
	if (bp->bt)
		return 1;
	if (bp->looked)
		return 0;
	bp->looked = 1;
	for (i = 0; i < RTGFB_NBOARDTAB; i++) {
		bt = &rtgfb_boardtab[i];
		rs = ms = 0;
		if (!autocon(((long)bt->manufacturer << 16) | bt->reg_product,
		    unit, &rb, &rs))
			continue;
		if (!autocon(((long)bt->manufacturer << 16) | bt->mem_product,
		    unit, &mb, &ms)) {
			printf("rtgfb: %s register board at 0x%x without its memory board\n",
			    bt->name, (unsigned int)rb);
			continue;
		}
		if (!rtgfb_z2ok((unsigned long)rb, (unsigned long)rs) ||
		    !rtgfb_z2ok((unsigned long)mb, (unsigned long)ms) ||
		    (unsigned long)rs < bt->reg_min ||
		    (unsigned long)ms < bt->mem_min) {
			printf("rtgfb: %s at 0x%x+0x%x / 0x%x+0x%x not usable\n",
			    bt->name, (unsigned int)rb, (unsigned int)rs,
			    (unsigned int)mb, (unsigned int)ms);
			continue;
		}
#ifdef RTGFB_KVA
		/* NOSLEEP: rtgfbinit runs from the boot driver chain; kept for
		 * good (a mapping may outlive any one open) */
		bp->reg_kva = (unsigned long)dev_kvmap((unsigned long)rb,
		    (unsigned long)rs, RTGFB_CM_NCS, 1);
		if (!bp->reg_kva) {
			printf("rtgfb: %s registers at 0x%x: no kernel mapping\n",
			    bt->name, (unsigned int)rb);
			continue;
		}
#endif
		bp->reg_phys = (unsigned long)rb;
		bp->reg_size = (unsigned long)rs;
		bp->mem_phys = (unsigned long)mb;
		bp->mem_size = (unsigned long)ms;
		bp->monitor = SVGAMONITORSWITCH_Amiga;
#ifdef RTGFB_FB_NC
		/* before the first mmap fault on it: the selector is fixed then */
		if (hat_cm_fb_add(phystopfn(bp->mem_phys),
		    phystopfn(bp->mem_phys + bp->mem_size)))
			printf("rtgfb: framebuffer noncacheable-not-serialised\n");
		else
			printf("rtgfb: framebuffer cache class not registered (stays serialised)\n");
#endif
		bp->bt = bt;
		bp->name = bt->name;
		if (bt->board == RTGFB_BOARD_PICASSO2)
			bp->name = rtgfb_p2model(bp);
		printf("rtgfb: %s, registers 0x%x+0x%x, memory 0x%x+0x%x\n",
		    bp->name, (unsigned int)rb, (unsigned int)rs,
		    (unsigned int)mb, (unsigned int)ms);
		return 1;
	}
	/*
	 * No board: silent.  Each driver reports only the hardware it finds
	 * (RTGFB_DEBUG keeps the line for bring-up).
	 */
#ifdef RTGFB_DEBUG
	printf("rtgfb: no supported graphics board\n");
#endif
	return 0;
}

/*
 * Boot: the driver chain amixdrv_init (io_init[0], after parinit) calls this
 * once: the one AutoConfig search, the board named on the console, silent
 * without one.  It does not call parinit: the chain does, first.
 */
void
rtgfbinit()
{
	(void) rtgfb_attach(0);
}

int
rtgfbopen(devp, flag, otyp, cr)
dev_t *devp;
int flag;
int otyp;
struct cred *cr;
{
	return rtgfb_attach((int)getminor(*devp)) ? 0 : ENXIO;
}

/* the last close: back to the Amiga video (a crashed or killed server too) */
int
rtgfbclose(dev, flag, otyp, cr)
dev_t dev;
int flag;
int otyp;
struct cred *cr;
{
	int unit;

	unit = (int)getminor(dev);
	if (unit >= 0 && unit < RTGFB_MAXBOARDS && rtgfb_boards[unit].bt &&
	    rtgfb_boards[unit].monitor != SVGAMONITORSWITCH_Amiga)
		rtgfb_switch(&rtgfb_boards[unit], SVGAMONITORSWITCH_Amiga);
	return 0;
}

int
rtgfbioctl(dev, cmd, arg, mode, cr, rvalp)
dev_t dev;
int cmd;
int arg;
int mode;
struct cred *cr;
int *rvalp;
{
	struct rtgfb_board *bp;
	struct rtgfb_info info;
	int unit, sw, i;

	unit = (int)getminor(dev);
	if (!rtgfb_attach(unit))
		return ENXIO;
	bp = &rtgfb_boards[unit];

	if (cmd == RTGFBIOC_GETINFO) {
		bzero((caddr_t)&info, sizeof info);
		info.version = RTGFB_VERSION;
		info.board = bp->bt->board;
		info.flags = bp->bt->flags;
		info.manufacturer = bp->bt->manufacturer;
		info.reg_product = bp->bt->reg_product;
		info.mem_product = bp->bt->mem_product;
		info.reg_phys = bp->reg_phys;
		info.reg_size = bp->reg_size;
		info.mem_phys = bp->mem_phys;
		info.mem_size = bp->mem_size;
		info.mmap_reg = RTGFB_MMAP_REG;
		info.mmap_mem = RTGFB_MMAP_MEM;
		info.monitor = bp->monitor;
		for (i = 0; i < RTGFB_NAMELEN - 1 && bp->name[i]; i++)
			info.name[i] = bp->name[i];
		if (copyout((caddr_t)&info, (caddr_t)arg, sizeof info))
			return EFAULT;
		return 0;
	}
	if (cmd == SVGAIOCGetMonitorSwitch) {
		sw = bp->monitor;
		if (copyout((caddr_t)&sw, (caddr_t)arg, sizeof sw))
			return EFAULT;
		return 0;
	}
	if (cmd == SVGAIOCSetMonitorSwitch) {
		if (copyin((caddr_t)arg, (caddr_t)&sw, sizeof sw))
			return EFAULT;
		if (sw != SVGAMONITORSWITCH_Amiga && sw != SVGAMONITORSWITCH_SVGA)
			return EINVAL;
		if (!(bp->bt->flags & RTGFB_F_MONSW))
			return ENXIO;
		rtgfb_switch(bp, sw);
		return 0;
	}
	return EINVAL;
}

/* the page frame of device offset `off`, -1 outside the two windows */
int
rtgfbmmap(dev, off, prot)
dev_t dev;
off_t off;
int prot;
{
	struct rtgfb_board *bp;
	unsigned long o;
	int unit;

	unit = (int)getminor(dev);
	if (unit < 0 || unit >= RTGFB_MAXBOARDS || !rtgfb_boards[unit].bt ||
	    off < 0)
		return -1;
	bp = &rtgfb_boards[unit];
	o = (unsigned long)off;
	if (o >= RTGFB_MMAP_MEM && o - RTGFB_MMAP_MEM < bp->mem_size)
		return (int)phystopfn(bp->mem_phys + (o - RTGFB_MMAP_MEM));
	if (o >= RTGFB_MMAP_REG && o - RTGFB_MMAP_REG < bp->reg_size)
		return (int)phystopfn(bp->reg_phys + (o - RTGFB_MMAP_REG));
	return -1;
}
