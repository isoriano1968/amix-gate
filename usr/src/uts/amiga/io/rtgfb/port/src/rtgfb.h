/*
 * rtgfb.h -- the interface of the rtgfb driver (/dev/rtgfb, character major 54).
 *
 * rtgfb is the small generic kernel side of AMIX graphics cards whose mode
 * setting and drawing run in user space (the X server's card module): it
 * finds the board through AutoConfig, describes it, maps its registers and
 * its framebuffer for mmap(2) and switches the monitor between the Amiga
 * video and the card.  Nothing else: no mode setting, no blitter, no console.
 *
 * The X server keeps its own copy of these definitions (xzz9000's
 * rtg/cirrus/cirrus.h); a change here is a change of RTGFB_VERSION.
 */
#ifndef RTGFB_H
#define RTGFB_H

#define RTGFB_DEV       "/dev/rtgfb"
#define RTGFB_MAJOR     54
#define RTGFB_VERSION   1

/*
 * The device's address space for mmap(2) (and nothing else: no read/write).
 * Both windows start on a page boundary on every kernel (2 KB and 4 KB pages).
 */
#define RTGFB_MMAP_MEM  0x00000000UL    /* the framebuffer (memory board)   */
#define RTGFB_MMAP_REG  0x01000000UL    /* the register board               */

/* rtgfb_info.board: which board family, for the user-space card module */
#define RTGFB_BOARD_PICASSO2    1       /* Village Tronic Picasso II/II+:
                                           Cirrus GD5426/5428, Zorro II   */

/* rtgfb_info.flags */
#define RTGFB_F_MONSW   0x0001          /* the board has a monitor switch   */

#define RTGFB_NAMELEN   32

struct rtgfb_info {
	unsigned long   version;        /* RTGFB_VERSION                     */
	unsigned long   board;          /* RTGFB_BOARD_*                     */
	unsigned long   flags;          /* RTGFB_F_*                         */
	unsigned short  manufacturer;   /* AutoConfig manufacturer           */
	unsigned char   reg_product;    /*   product of the register board   */
	unsigned char   mem_product;    /*   product of the memory board     */
	unsigned long   reg_phys;       /* register board: physical address  */
	unsigned long   reg_size;       /*   and its size (AutoConfig)       */
	unsigned long   mem_phys;       /* memory board: physical address    */
	unsigned long   mem_size;       /*   and its size (AutoConfig)       */
	unsigned long   mmap_reg;       /* mmap offsets of the two windows   */
	unsigned long   mmap_mem;
	int             monitor;        /* SVGAMONITORSWITCH_* now           */
	char            name[RTGFB_NAMELEN];
};

#define RTGFBIOC                (('r' << 8))
#define RTGFBIOC_GETINFO        (RTGFBIOC | 1)  /* struct rtgfb_info *       */

/*
 * The monitor switch of the RTG contract (Klaus Burckert's Xsvga numbering,
 * as in Antti Sokero's va2000.h and our zz9000.h): the X server's rtg/ layer
 * sends Set(SVGA) once the mode is set and Set(Amiga) when it ends.  The
 * argument is an int *.  The driver also switches back to the Amiga video on
 * the device's last close, so a server that dies leaves the Amiga picture.
 */
#ifndef SVGAIOCSetMonitorSwitch
#define SVGAIOC                 (0xe300)
#define SVGAIOCGetMonitorSwitch (SVGAIOC|0x06)
#define SVGAIOCSetMonitorSwitch (SVGAIOC|0x08)  /* note: 0x08 not 0x07 */
#define SVGAMONITORSWITCH_Amiga 1
#define SVGAMONITORSWITCH_SVGA  2
#endif

#endif /* RTGFB_H */
