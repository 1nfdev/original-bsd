/*
 * Copyright (c) 1993 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Christos Zoulas.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "@(#)for.c	5.1 (Berkeley) 05/24/93";
#endif /* not lint */

/*-
 * for.c --
 *	Functions to handle loops in a makefile.
 *
 * Interface:
 *	For_Eval 	Evaluate the loop in the passed line.
 *	For_Run		Run accumulated loop
 *
 */

#include    <ctype.h>
#include    "make.h"
#include    "hash.h"
#include    "dir.h"
#include    "buf.h"

/*
 * For statements are of the form:
 *
 * .for <variable> in <varlist>
 * ...
 * .endfor
 *
 * The trick is to look for the matching end inside for for loop
 * To do that, we count the current nesting level of the for loops.
 * and the .endfor statements, accumulating all the statements between
 * the initial .for loop and the matching .endfor; 
 * then we evaluate the for loop for each variable in the varlist.
 */

static int  	  forLevel = 0;  	/* Nesting level	*/
static char	 *forVar;		/* Iteration variable	*/
static Buffer	  forBuf;		/* Commands in loop	*/
static Lst	  forLst;		/* List of items	*/

/*
 * State of a for loop.
 */
struct For {
    Buffer	  buf;			/* Unexpanded buffer	*/
    char*	  var;			/* Index name		*/
    Lst  	  lst;			/* List of variables	*/
};

static int ForExec	__P((char *, struct For *));




/*-
 *-----------------------------------------------------------------------
 * For_Eval --
 *	Evaluate the for loop in the passed line. The line
 *	looks like this:
 *	    .for <variable> in <varlist>
 *
 * Results:
 *	TRUE: We found a for loop, or we are inside a for loop
 *	FALSE: We did not find a for loop, or we found the end of the for
 *	       for loop.
 *
 * Side Effects:
 *	None.
 *
 *-----------------------------------------------------------------------
 */
int
For_Eval (line)
    char    	    *line;    /* Line to parse */
{
    char	    *ptr = line, *sub, *wrd;
    int	    	    level;  	/* Level at which to report errors. */

    level = PARSE_FATAL;


    if (forLevel == 0) {
	Buffer	    buf;
	int	    varlen;

	for (ptr++; *ptr && isspace(*ptr); ptr++)
	    continue;
	/*
	 * If we are not in a for loop quickly determine if the statement is
	 * a for.
	 */
	if (ptr[0] != 'f' || ptr[1] != 'o' || ptr[2] != 'r' || !isspace(ptr[3]))
	    return FALSE;
	ptr += 3;
	
	/*
	 * we found a for loop, and now we are going to parse it.
	 */
	while (*ptr && isspace(*ptr))
	    ptr++;
	
	/*
	 * Grab the variable
	 */
	buf = Buf_Init(0);
	for (wrd = ptr; *ptr && !isspace(*ptr); ptr++) 
	    continue;
	Buf_AddBytes(buf, ptr - wrd, (Byte *) wrd);

	forVar = (char *) Buf_GetAll(buf, &varlen);
	if (varlen == 0) {
	    Parse_Error (level, "missing variable in for");
	    return 0;
	}
	Buf_Destroy(buf, FALSE);

	while (*ptr && isspace(*ptr))
	    ptr++;

	/*
	 * Grab the `in'
	 */
	if (ptr[0] != 'i' || ptr[1] != 'n' || !isspace(ptr[2])) {
	    Parse_Error (level, "missing `in' in for");
	    printf("%s\n", ptr);
	    return 0;
	}
	ptr += 3;

	while (*ptr && isspace(*ptr))
	    ptr++;

	/*
	 * Make a list with the remaining words
	 */
	forLst = Lst_Init(FALSE);
	buf = Buf_Init(0);
	sub = Var_Subst(NULL, ptr, VAR_GLOBAL, FALSE); 

#define ADDWORD() \
	Buf_AddBytes(buf, ptr - wrd, (Byte *) wrd), \
	Buf_AddByte(buf, (Byte) '\0'), \
	Lst_AtEnd(forLst, (ClientData) Buf_GetAll(buf, &varlen)), \
	Buf_Destroy(buf, FALSE)

	for (ptr = sub; *ptr && isspace(*ptr); ptr++)
	    continue;

	for (wrd = ptr; *ptr; ptr++)
	    if (isspace(*ptr)) {
		ADDWORD();
		buf = Buf_Init(0);
		while (*ptr && isspace(*ptr))
		    ptr++;
		wrd = ptr--;
	    }
	if (DEBUG(FOR))
	    (void) fprintf(stderr, "For: Iterator %s List %s\n", forVar, sub);
	if (ptr - wrd > 0) 
	    ADDWORD();
	else
	    Buf_Destroy(buf, TRUE);
	free((Address) sub);
	    
	forBuf = Buf_Init(0);
	forLevel++;
	return 1;
    }
    else if (*ptr == '.') {

	for (ptr++; *ptr && isspace(*ptr); ptr++)
	    continue;

	if (strncmp(ptr, "endfor", 6) == 0 && (isspace(ptr[6]) || !ptr[6])) {
	    if (DEBUG(FOR))
		(void) fprintf(stderr, "For: end for %d\n", forLevel);
	    if (--forLevel < 0) {
		Parse_Error (level, "for-less endfor");
		return 0;
	    }
	}
	else if (strncmp(ptr, "for", 3) == 0 && isspace(ptr[3])) {
	    forLevel++;
	    if (DEBUG(FOR))
		(void) fprintf(stderr, "For: new loop %d\n", forLevel);
	}
    }

    if (forLevel != 0) {
	Buf_AddBytes(forBuf, strlen(line), (Byte *) line);
	Buf_AddByte(forBuf, (Byte) '\n');
	return 1;
    }
    else {
	return 0;
    }
}

/*-
 *-----------------------------------------------------------------------
 * ForExec --
 *	Expand the for loop for this index and push it in the Makefile
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None.
 *
 *-----------------------------------------------------------------------
 */
static int
ForExec(name, arg)
    char *name;
    struct For *arg;
{
    int len;
    Var_Set(arg->var, name, VAR_GLOBAL);
    if (DEBUG(FOR))
	(void) fprintf(stderr, "--- %s = %s\n", arg->var, name);
    Parse_FromString(Var_Subst(arg->var, (char *) Buf_GetAll(arg->buf, &len), 
			       VAR_GLOBAL, FALSE));
    Var_Delete(arg->var, VAR_GLOBAL);

    return 0;
}


/*-
 *-----------------------------------------------------------------------
 * For_Run --
 *	Run the for loop, immitating the actions of an include file
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None.
 *
 *-----------------------------------------------------------------------
 */
void
For_Run()
{
    struct For arg;

    if (forVar == NULL || forBuf == NULL || forLst == NULL)
	return;
    arg.var = forVar;
    arg.buf = forBuf;
    arg.lst = forLst;
    forVar = NULL;
    forBuf = NULL;
    forLst = NULL;

    Lst_ForEach(arg.lst, ForExec, (ClientData) &arg);

    free((Address)arg.var);
    Lst_Destroy(arg.lst, free);
    Buf_Destroy(arg.buf, TRUE);
}