/*
 * vt040.c -- virtual terminals on the Z3660 RTG for a 68040/68060 kernel
 * (relink-040-vt.sh, in place of relink-040-c1hook.sh).  Alt+F1..F10 and
 * Alt+Esc switch between the stock text consoles and an X session on the
 * RTG, both ways.
 *
 * - The text consoles keep their stock rendering into their chip-RAM
 *   bitmaps (the native output shows them as before).  The displayed one is
 *   copied, scaled, into the console buffer of the zz9000 driver
 *   (zz9000vtinfo) and the card is panned onto that buffer (zz9000vtpan).
 * - An X session on the RTG is a screen without a bitmap.  Stock
 *   DisplayScreen would build an ECS copper list for it (scrdev.c scrmisc ->
 *   scrcop) and LoadView it; DisplayScreen here does the same bookkeeping
 *   without either, and the card is panned back to offset 0, where the X
 *   server's pixels are untouched.  The native output keeps the last console.
 * - X only does SIOCACTIVATE (SelectScreen) on its screen, so selecting a
 *   screen without a bitmap displays it too: X becomes the displayed screen
 *   of its group, and its Alt+F-key brings it back.
 * - Leaving a raw-key screen (X), the screen gets key releases for the
 *   qualifier and function keys, or X keeps the Alt of Alt+F1 pressed.
 *
 * The copy runs from a timeout at VT_HZ, VT_ROWS rows a tick, so a full
 * redraw never holds the clock interrupt for long.  A shadow of the planes as
 * last drawn tells which rows changed; zz9000vtdirty (bumped by console
 * output here and by the kernel printf path in zz9000.c) says when to look.
 *
 * Nothing changes until the console redirect is on (zz9000vtinfo fails
 * before): then the RTG console's own drawing stops and this mirror shows.
 */

#include "sys/types.h"
#include "sys/param.h"
#include "sys/systm.h"
#include "sys/inline.h"
#include "screen.h"

#define VT_HZ		20		/* copies a second while a console shows */
#define VT_ROWS		48		/* source rows drawn per tick, at most */
#define VT_CLEAR	24		/* border rows cleared per tick */
#define VT_MAXBPR	80		/* bitmap bytes per row: 640 pixels */
#define VT_MAXH		512		/* bitmap rows */
#define VT_MAXD		4		/* planes mirrored (16 colours) */
#define VT_MAXS		6		/* largest scale */

#define KQUAL_FIRST	0x60		/* shift, caps, ctrl, alt, amiga keys */
#define KQUAL_LAST	0x67
#define KFUNC_FIRST	0x50		/* F1..F10 */
#define KFUNC_LAST	0x59

extern struct screen *displayedscreen, *activescreen;
extern struct screen *menuscreen;
extern unsigned long screenstamp;
extern int stampwaiting;

extern void DisplayScreen_stock(), SelectScreen_stock(), scrcon_put_stock();
extern int zz9000vtinfo();
extern void zz9000vtpan();
extern int zz9000vtdirty;

static struct screen *vt_shown;		/* screen in the buffer, 0 = none */
static struct screen *vt_last;		/* screen laid out last (still drawn) */
static int vt_tid;			/* pending timeout, 0 = none */
static char *vt_fb;			/* the buffer, kernel mapping */
static int vt_w, vt_h;			/* its mode */
static int vt_bw, vt_bh, vt_bpr, vt_depth;	/* the bitmap drawn */
static int vt_sx, vt_sy, vt_x0, vt_y0;	/* scale and origin */
static int vt_clear;			/* buffer rows cleared so far */
static int vt_full;			/* 1: redraw every row, from vt_next */
static int vt_next;			/* next row of a full redraw */
static int vt_offset;			/* bitmap offset as last drawn */
static unsigned short vt_pal[1 << VT_MAXD];	/* RGB565 of the colours */
static unsigned char vt_shadow[VT_MAXD][VT_MAXH][VT_MAXBPR];
static unsigned short vt_line[VT_MAXBPR * 8 * VT_MAXS];	/* one scaled row */

static void vt_tick();

/* an ECS 0x0RGB colour register as RGB565 */
static unsigned short vt_565(c)
unsigned short c;
{
    unsigned r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;

    return (unsigned short)(((r << 1 | r >> 3) << 11) |
	((g << 2 | g >> 2) << 5) | (b << 1 | b >> 3));
}

static void vt_kick()
{
    if (!vt_tid)
	vt_tid = timeout(vt_tick, (caddr_t)0,
	    (long)(HZ / VT_HZ ? HZ / VT_HZ : 1));
}

/* source row of displayed row r: the console scrolls by moving the offset */
static int vt_src(bp, r)
struct bitmap *bp;
int r;
{
    int src = r + bp->offset;

    return src >= vt_bh ? src - vt_bh : src;
}

/*
 * Lay out the displayed console in the buffer: the largest integer scale
 * that fits, twice as tall as wide on a non-interlaced screen (its pixels
 * are), centred.  1 = ready to draw, 0 = nothing to draw.
 */
static int vt_layout(sp)
struct screen *sp;
{
    struct bitmap *bp = sp->bmap;
    int s;

    vt_bw = bp->width;
    vt_bh = bp->height;
    vt_bpr = bp->width / 8;
    vt_depth = bp->depth < VT_MAXD ? bp->depth : VT_MAXD;
    if (vt_bpr < 1 || vt_bpr > VT_MAXBPR || vt_bh < 1 || vt_bh > VT_MAXH ||
	vt_depth < 1)
	return 0;
    for (s = VT_MAXS; s > 1; --s)
	if (vt_bw * s <= vt_w &&
	    vt_bh * ((sp->modes & Sm_ILACE) ? s : 2 * s) <= vt_h)
	    break;
    vt_sx = s;
    vt_sy = (sp->modes & Sm_ILACE) ? s : 2 * s;
    if (vt_bw * vt_sx > vt_w || vt_bh * vt_sy > vt_h)
	vt_sx = vt_sy = 1;
    if (vt_bw * vt_sx > vt_w || vt_bh * vt_sy > vt_h)
	return 0;
    vt_x0 = (vt_w - vt_bw * vt_sx) / 2;
    vt_y0 = (vt_h - vt_bh * vt_sy) / 2;
    vt_clear = 0;
    vt_full = 1;
    vt_next = 0;
    vt_offset = bp->offset;
    return 1;
}

/* show the console buffer for a displayed text console, the screen for X */
static void vt_follow()
{
    struct screen *sp = displayedscreen;

    if (zz9000vtinfo(&vt_fb, &vt_w, &vt_h))
	return;
    if (sp && sp->bmap) {
	if (sp != vt_shown) {
	    /* the same console again: the buffer still holds its picture */
	    if (sp == vt_last && sp->bmap->width == vt_bw &&
		sp->bmap->height == vt_bh && vt_clear >= vt_h) {
		vt_full = 1;
		vt_next = 0;
	    } else if (!vt_layout(sp)) {
		vt_shown = vt_last = (struct screen *)0;
		zz9000vtpan(0);
		return;
	    }
	}
	vt_shown = vt_last = sp;
	zz9000vtpan(1);
	vt_kick();
    } else {
	vt_shown = (struct screen *)0;
	zz9000vtpan(0);
    }
}

/* one buffer row: black outside the image, its borders inside */
static void vt_clear_row(y)
int y;
{
    unsigned short *d = (unsigned short *)(vt_fb + y * vt_w * 2);
    int x;

    if (y < vt_y0 || y >= vt_y0 + vt_bh * vt_sy) {
	for (x = 0; x < vt_w; ++x)
	    d[x] = 0;
	return;
    }
    for (x = 0; x < vt_x0; ++x)
	d[x] = 0;
    for (x = vt_x0 + vt_bw * vt_sx; x < vt_w; ++x)
	d[x] = 0;
}

/* does displayed row r differ from the shadow */
static int vt_changed(bp, r)
struct bitmap *bp;
int r;
{
    int src = vt_src(bp, r), p, x;
    unsigned char *a, *b;

    for (p = 0; p < vt_depth; ++p) {
	a = bp->bpl[p] + src * vt_bpr;
	b = vt_shadow[p][r];
	for (x = 0; x < vt_bpr; ++x)
	    if (a[x] != b[x])
		return 1;
    }
    return 0;
}

/* displayed row r into the shadow and, scaled, into the buffer */
static void vt_draw_row(bp, r)
struct bitmap *bp;
int r;
{
    int src = vt_src(bp, r), y, i, k, x, p, b;
    unsigned short *o;

    for (p = 0; p < vt_depth; ++p)
	bcopy((caddr_t)(bp->bpl[p] + src * vt_bpr),
	    (caddr_t)vt_shadow[p][r], vt_bpr);
    o = vt_line;
    for (x = 0; x < vt_bpr; ++x)
	for (b = 7; b >= 0; --b) {
	    for (i = p = 0; p < vt_depth; ++p)
		i |= ((vt_shadow[p][r][x] >> b) & 1) << p;
	    for (k = vt_sx; k > 0; --k)
		*o++ = vt_pal[i];
	}
    for (y = 0; y < vt_sy; ++y)
	bcopy((caddr_t)vt_line, (caddr_t)(vt_fb +
	    ((vt_y0 + r * vt_sy + y) * vt_w + vt_x0) * 2),
	    vt_bw * vt_sx * 2);
}

static void vt_tick()
{
    struct screen *sp = vt_shown;
    struct bitmap *bp;
    int s, r, p, n, w, h;
    unsigned short pal;
    char *fb;

    vt_tid = 0;
    if (!sp || sp != displayedscreen || !(bp = sp->bmap))
	return;
    s = splscr();
    if (zz9000vtinfo(&fb, &w, &h)) {
	splx(s);
	return;
    }
    if (w != vt_w || h != vt_h || bp->width != vt_bw || bp->height != vt_bh) {
	vt_fb = fb;
	vt_w = w;
	vt_h = h;
	if (!vt_layout(sp)) {
	    splx(s);
	    return;
	}
    }
    for (p = 0; p < (1 << vt_depth); ++p) {	/* a colour change: all */
	pal = vt_565(sp->color[p]);
	if (pal != vt_pal[p]) {
	    vt_pal[p] = pal;
	    vt_full = 1;
	    vt_next = 0;
	}
    }
    if (bp->offset != vt_offset) {		/* a scroll: every row moved */
	vt_offset = bp->offset;
	vt_full = 1;
	vt_next = 0;
    }
    if (vt_full) {				/* full redraw, a slice a tick */
	for (n = VT_ROWS; n > 0 && vt_next < vt_bh; --n)
	    vt_draw_row(bp, vt_next++);
	if (vt_next >= vt_bh)
	    vt_full = 0;
    } else if (vt_clear < vt_h) {		/* then the borders */
	for (n = VT_CLEAR; n > 0 && vt_clear < vt_h; --n)
	    vt_clear_row(vt_clear++);
    } else if (zz9000vtdirty) {			/* changed rows only */
	zz9000vtdirty = 0;
	for (r = n = 0; r < vt_bh; ++r)
	    if (vt_changed(bp, r)) {
		if (n++ >= VT_ROWS) {
		    zz9000vtdirty = 1;		/* more next tick */
		    break;
		}
		vt_draw_row(bp, r);
	    }
    }
    vt_kick();
    splx(s);
}

/* stock DisplayScreen's bookkeeping for a screen without a bitmap */
static void vt_display_rtg(sp)
struct screen *sp;
{
    if (displayedscreen) {
	displayedscreen->flags &= ~Sf_DISPLAY;
	(*displayedscreen->mifunc)(displayedscreen, SC_CHANGE);
    }
    if (!sp->group && displayedscreen && displayedscreen->group) {
	sp->group = displayedscreen->group;
	sp->next = sp->group->top;
	sp->group->top = sp;
    }
    sp->flags &= ~Sf_NEEDCOP;		/* scrmisc must build no copper list */
    sp->flags |= Sf_DISPLAY;
    (*sp->mifunc)(sp, SC_CHANGE);
    displayedscreen = sp;
    ++screenstamp;
    if (stampwaiting) {
	stampwaiting = 0;
	wakeup((caddr_t)&stampwaiting);
    }
    if (menuscreen)
	(*menuscreen->kbfunc)(menuscreen, (unsigned long)IETYPE_MENU << 24, 0L);
}

void DisplayScreen(sp)
struct screen *sp;
{
    int s = splscr();

    if (sp && !sp->bmap && sp != displayedscreen)
	vt_display_rtg(sp);
    else
	DisplayScreen_stock(sp);
    vt_follow();
    splx(s);
}

/* key releases for a raw-key screen losing the keyboard */
static void vt_release(sp)
struct screen *sp;
{
    int c;

    for (c = KQUAL_FIRST; c <= KQUAL_LAST; ++c)
	(*sp->kbfunc)(sp, 0x01000000L | 0x80 | c, 0L);
    for (c = KFUNC_FIRST; c <= KFUNC_LAST; ++c)
	(*sp->kbfunc)(sp, 0x01000000L | 0x80 | c, 0L);
}

void SelectScreen(sp)
struct screen *sp;
{
    int s = splscr();
    char *fb;
    int w, h;

    if (activescreen && activescreen != sp &&
	(activescreen->flags & Sf_RAWKEY) && activescreen->kbfunc)
	vt_release(activescreen);
    /* X: selecting it displays it (it never asks for DisplayScreen) */
    if (sp && !sp->bmap && sp != displayedscreen && sp->mifunc &&
	!zz9000vtinfo(&fb, &w, &h)) {
	vt_display_rtg(sp);
	vt_follow();
    }
    SelectScreen_stock(sp);
    splx(s);
}

void scrcon_put(cp, p, n)
caddr_t cp;
caddr_t p;
int n;
{
    scrcon_put_stock(cp, p, n);
    ++zz9000vtdirty;
    /* the first output once the redirect is on starts the mirror */
    if (!vt_shown && !vt_tid && displayedscreen && displayedscreen->bmap) {
	int s = splscr();

	vt_follow();
	splx(s);
    }
}
