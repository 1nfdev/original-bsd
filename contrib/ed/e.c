/*-
 * Copyright (c) 1992 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rodney Ruddock of the University of Guelph.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "@(#)e.c	5.4 (Berkeley) 03/01/93";
#endif /* not lint */

#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <limits.h>
#include <regex.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef DBI
#include <db.h>
#endif

#include "ed.h"
#include "extern.h"

/*
 * Places a new file in the buffer to be editted. The current contents
 * of the buffer are deleted - no undo can be perfomed. A warning is
 * issued once if no write has occurred since the last buffer
 * modification (unless 'E' spec'd).
 */

void
e(inputt, errnum)
	FILE *inputt;
	int *errnum;
{
	int l_which;		/* which is it? 'e' or 'E' */
	char *l_temp;

	l_which = ss;

	l_temp = filename(inputt, errnum);
	if (*errnum == 1) {
		sigspecial++;
		free(filename_current);
		filename_current = l_temp;
		sigspecial--;
		if (sigint_flag && (!sigspecial))
			SIGINT_ACTION;
	} else
		if (*errnum == -2)
			while (((ss = getc(inputt)) != '\n') || (ss == EOF));
		else
			if (*errnum < 0)
				return;
	*errnum = 0;

	/* Note: 'E' will bypass this if stmt., which warns of no save. */
	if ((change_flag == 1L) && (l_which == 'e')) {
		change_flag = 0L;
		strcpy(help_msg, "warning: buffer changes not saved");
		*errnum = -1;
		ungetc('\n', inputt);
		return;
	}
	start = top;
	End = bottom;
	start_default = End_default = 0;
	if (start == NULL && bottom == NULL);
	else {
		ungetc(ss, inputt);
		d(inputt, errnum);	/* delete the whole buffer */
	}

	/* An 'e' clears all traces of last doc'mt, even in 'g'. */
	u_clr_stk();
	if (*errnum < 0)
		return;
	*errnum = 0;
#ifdef STDIO
	if (fhtmp > NULL) {
		fclose(fhtmp);
		unlink(template);
	}
#endif
#ifdef DBI
	if (dbhtmp != NULL) {
		(dbhtmp->close) (dbhtmp);
		unlink(template);
	}
#endif

	name_set = 1;
	e2(inputt, errnum);

	*errnum = 1;
}

/*
 * This is pulled out of e.c to make the "simulated 'e'" at startup easier to
 * handle.
 */
void
e2(inputt, errnum)
	FILE *inputt;
	int *errnum;
{
	char *tmp_path;
#ifdef DBI
	RECNOINFO l_dbaccess;
#endif

	sigspecial++;
#ifndef MEMORY
	if (template == NULL) {
		template = (char *) calloc(FILENAME_LEN, sizeof(char));
		if (template == NULL)
			ed_exit(4);
	}
	/* create the buffer using the method favored at compile time */
	tmp_path = getenv("TMPDIR");
	sprintf(template, "%s/_4.4bsd_ed_XXXXXX", tmp_path ? tmp_path : "/tmp");
	mktemp(template);
#endif
#ifdef STDIO
	fhtmp = fopen(template, "w+");
	if (fhtmp == NULL) {
		ed_exit(5); /* unable to create buffer */
	}
	fwrite("R", sizeof(char), 1, fhtmp);
	file_seek = 0;
#endif
#ifdef DBI
	(l_dbaccess.bval) = (u_char) '\0';
	(l_dbaccess.cachesize) = 0;
	(l_dbaccess.flags) = R_NOKEY;
	(l_dbaccess.lorder) = 0;
	(l_dbaccess.reclen) = 0;
	dbhtmp = dbopen(template, O_CREAT | O_RDWR,
	    S_IRUSR | S_IWUSR, (DBTYPE) DB_RECNO, &l_dbaccess);
	if (dbhtmp == NULL) {
		ed_exit(5); /* unable to create buffer */
	}
#endif
	current = top;
	start = top;
	End = bottom;

	sigspecial--;
	if (sigint_flag &&(!sigspecial))
		SIGINT_ACTION;

	change_flag = 1;
	if (name_set) {
		/* So 'r' knows the filename is already read in. */
		filename_flag = 1;
		r(inputt, errnum);
	}
	change_flag = 0;
}
