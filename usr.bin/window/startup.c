/*
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Edward Wang at The University of California, Berkeley.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "@(#)startup.c	8.1 (Berkeley) 06/06/93";
#endif /* not lint */

#include "defs.h"
#include "value.h"
#include "var.h"
#include "char.h"
#include "local.h"

doconfig()
{
	char buf[100];
	char *home;
	static char runcom[] = RUNCOM;

	if ((home = getenv("HOME")) == 0)
		home = ".";
	(void) sprintf(buf, "%.*s/%s",
		(sizeof buf - sizeof runcom) / sizeof (char) - 1,
		home, runcom);
	return dosource(buf);
}

/*
 * The default is two windows of equal size.
 */
dodefault()
{
	struct ww *w;
	register r = wwnrow / 2 - 1;

	if (openwin(1, r + 2, 0, wwnrow - r - 2, wwncol, default_nline,
		(char *) 0, 1, 1, default_shellfile, default_shell) == 0)
		return;
	if ((w = openwin(0, 1, 0, r, wwncol, default_nline,
		(char *) 0, 1, 1, default_shellfile, default_shell)) == 0)
		return;
	wwprintf(w, "Escape character is %s.\r\n", unctrl(escapec));
}

setvars()
{
	/* try to use a random ordering to balance the tree */
	(void) var_setnum("nrow", wwnrow);
	(void) var_setnum("ncol", wwncol);
	(void) var_setnum("baud", wwbaud);
	(void) var_setnum("m_rev", WWM_REV);
	(void) var_setnum("m_blk", WWM_BLK);
	(void) var_setnum("m_ul", WWM_UL);
	(void) var_setnum("m_grp", WWM_GRP);
	(void) var_setnum("m_dim", WWM_DIM);
	(void) var_setnum("m_usr", WWM_USR);
	(void) var_setstr("term", wwterm);
	(void) var_setnum("modes", wwavailmodes);
}
