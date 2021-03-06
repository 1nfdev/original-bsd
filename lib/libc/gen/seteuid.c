/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)seteuid.c	5.4 (Berkeley) 06/01/90";
#endif /* LIBC_SCCS and not lint */

seteuid(euid)
	int euid;
{

	return (setreuid(-1, euid));
}
