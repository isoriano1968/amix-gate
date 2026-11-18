/* SPDX-License-Identifier: MIT */
/*
 * buddha_kit.c -- the Individual Computers Buddha and Catweasel Z-II IDE
 * driver of amix-gayle-ide (../../src/ide.c, Emre Bastuz, MIT; the amigaux
 * fork at the commit ../../UPSTREAM names) linked into an AMIX kernel by the
 * AMIX Installer's buddha kit, for kernels without the amigaux module core.
 *
 * ide.c's Buddha back end exists only in its module form (IDE_BUDDHA implies
 * IDE_MODULE): the load entry buddha_load() finds the boards with autocon()
 * and probes their ports, then registers each board with the module core's
 * sd_register().  Here the same code runs unchanged, and the four calls into
 * the module core are this file's:
 *
 *   sd_register()   nothing to do: sd.c's own init() finds the boards in its
 *                   controller table, where patch_buddha_card.py puts two
 *                   rows -- 0x12120000 (the Buddha) -> budqueue and 0x1212002A
 *                   (the Catweasel Z-II) -> cwqueue -- and calls them with the
 *                   board's index among the boards of that product.
 *   root_defer()    refused: interrupt I/O needs the module core's deferred
 *                   self-test (process context, after the root is mounted),
 *                   which a kernel without the core does not run.  The kit
 *                   loads the driver with intr=0: polled I/O, as the module
 *                   form falls back to when its self-test fails.
 *   intr_attach(),  never reached with intr=0.
 *   intr_detach()
 *
 * buddhainit() is the kit's boot routine (KIT.info init=, called by the
 * installer's io_init chain before the root is mounted): buddha_load("intr=0")
 * with the driver's own defaults otherwise (masters and slaves, units 6/5,
 * 3/2, 1/0 per port).  No board, or no drive: buddha_load() says nothing and
 * leaves nothing behind, and the queue functions answer every request with
 * CHECK CONDITION.
 *
 * Board indices: buddha_load() takes every Buddha first, then every Catweasel
 * Z-II, in autocon() order -- the order sd.c's init() walks them too -- so the
 * n-th Buddha is board n and the n-th Catweasel Z-II is board (Buddhas) + n.
 */
#define IDE_BUDDHA	1
#define IDE_MODULE	1
#include	"ide.c"

static int	bud_kit_nbuddha;	/* Buddha boards buddha_load() indexed */

/*ARGSUSED*/
int
sd_register( f, c, a, name, rank)
bool	(*f)();
uint	c;
char	*a, *name;
int	rank;
{
	return 0;
}

/*ARGSUSED*/
int
root_defer( f, arg)
int	(*f)();
char	*arg;
{
	return ENXIO;
}

/*ARGSUSED*/
int
intr_attach( level, f, arg, name)
int	level;
int	(*f)();
char	*arg, *name;
{
	return ENXIO;
}

/*ARGSUSED*/
int
intr_detach( level, f, arg)
int	level;
int	(*f)();
char	*arg;
{
	return 0;
}

void
buddhainit()
{
	int	b;

	if (buddha_load( "intr=0") != 0)
		return;
	for (b = 0; b < bud_nboards; ++b)
		if (bud_ch[b * BUD_NPORTS].name == bud_name_buddha)
			bud_kit_nbuddha++;
}

/* sd.c's queue entries: c is the board's index among its product's boards */
bool
budqueue( c, cp)
uint		c;
struct sdcom	*cp;
{
	return idequeue( (int)c, cp);
}

bool
cwqueue( c, cp)
uint		c;
struct sdcom	*cp;
{
	return idequeue( bud_kit_nbuddha + (int)c, cp);
}
