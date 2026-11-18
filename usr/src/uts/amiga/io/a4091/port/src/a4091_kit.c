/*
 * a4091_kit.c -- the AMIX Installer's a4091 kit: amigaux's static A4091 driver
 * (../../src/a4091-wr.c, Jussi Alanaerae, MIT, unchanged) linked into a kernel
 * that has no sdcardbase().
 *
 * The driver maps a board on its first request from the base sd.c recorded
 * for that card (sdcardbase(card)).  Stock sd.c keeps that base in a local
 * table, so the kit records it itself: the controller table rows of this kit
 * (a4091_card.s) call a4091q/a4092q/a4770q, which look the board up with
 * autocon() by its product and sd.c's per-product index c -- the same call
 * that put it into sd.c's queue -- before handing the request to a4091queue().
 *
 * Built with -Dsdcardbase=a4091_sdcardbase, so the name stays private to the
 * kit.  The 040/060 build adds -DA4091_KVA -DA4091_D42: the board pages are
 * mapped noncacheable by the port's dev_kvmap (the kit's own copy, named
 * a4091_kvmap) and every transfer goes through the port kernel's
 * dma_prepare/dma_complete (src/dma_cache040.s of the port line).
 */
#include "a4091-wr.c"

#define	A4K_A4091	0x02020054L	/* Commodore A4091 */
#define	A4K_A4092	0xc0de0001L	/* A4092 (SCSI.ME) */
#define	A4K_A4770	0xc0de0002L	/* A4770 (SCSI.ME) */

extern int	autocon();

static char	*a4k_base[SDCARDS];

char *
sdcardbase( card)
int	card;
{
	if (card < 0 || card >= SDCARDS)
		return 0;
	return a4k_base[card];
}

static bool
a4k_queue( pn, c, cp)
long		pn;
int		c;
struct sdcom	*cp;
{
	char	*a, *o;

	if (cp->card < SDCARDS && a4k_base[cp->card] == 0 && autocon( pn, c, &a, &o))
		a4k_base[cp->card] = a;
	return a4091queue( c, cp);
}

bool
a4091q( c, cp)
int		c;
struct sdcom	*cp;
{
	return a4k_queue( A4K_A4091, c, cp);
}

bool
a4092q( c, cp)
int		c;
struct sdcom	*cp;
{
	return a4k_queue( A4K_A4092, c, cp);
}

bool
a4770q( c, cp)
int		c;
struct sdcom	*cp;
{
	return a4k_queue( A4K_A4770, c, cp);
}
