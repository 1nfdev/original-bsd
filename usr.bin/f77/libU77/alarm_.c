/*-
 * Copyright (c) 1980 The Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.proprietary.c%
 */

#ifndef lint
static char sccsid[] = "@(#)alarm_.c	5.2 (Berkeley) 04/12/91";
#endif /* not lint */

/*
 * set an alarm time, arrange for user specified action, and return.
 *
 * calling sequence:
 *	integer	flag
 *	external alfunc
 *	lastiv = alarm (intval, alfunc)
 * where:
 *	intval	= the alarm interval in seconds; 0 turns off the alarm.
 *	alfunc	= the function to be called after the alarm interval,
 *
 *	The returned value will be the time remaining on the last alarm.
 */

#include <signal.h>

long alarm_(sec, proc)
long	*sec;
int	(* proc)();
{
	register long	lt;

	lt = alarm(1000);	/* time to maneuver */

	if (*sec)
		signal(SIGALRM, proc);

	alarm(*sec);
	return(lt);
}
