/*
 * Copyright (c) 1983 Eric P. Allman
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * %sccs.include.redist.c%
 */

# include "sendmail.h"
# include <signal.h>
# include <pwd.h>

#ifndef lint
static char sccsid[] = "@(#)alias.c	8.1 (Berkeley) 06/07/93";
#endif /* not lint */


MAP	*AliasDB[MAXALIASDB + 1];	/* actual database list */
int	NAliasDBs;			/* number of alias databases */
/*
**  ALIAS -- Compute aliases.
**
**	Scans the alias file for an alias for the given address.
**	If found, it arranges to deliver to the alias list instead.
**	Uses libdbm database if -DDBM.
**
**	Parameters:
**		a -- address to alias.
**		sendq -- a pointer to the head of the send queue
**			to put the aliases in.
**		e -- the current envelope.
**
**	Returns:
**		none
**
**	Side Effects:
**		Aliases found are expanded.
**
**	Deficiencies:
**		It should complain about names that are aliased to
**			nothing.
*/

alias(a, sendq, e)
	register ADDRESS *a;
	ADDRESS **sendq;
	register ENVELOPE *e;
{
	register char *p;
	int naliases;
	char *owner;
	char obuf[MAXNAME + 6];
	extern char *aliaslookup();

	if (tTd(27, 1))
		printf("alias(%s)\n", a->q_paddr);

	/* don't realias already aliased names */
	if (bitset(QDONTSEND|QBADADDR|QVERIFIED, a->q_flags))
		return;

	if (NoAlias)
		return;

	e->e_to = a->q_paddr;

	/*
	**  Look up this name
	*/

	p = aliaslookup(a->q_user, e);
	if (p == NULL)
		return;

	/*
	**  Match on Alias.
	**	Deliver to the target list.
	*/

	if (tTd(27, 1))
		printf("%s (%s, %s) aliased to %s\n",
		    a->q_paddr, a->q_host, a->q_user, p);
	if (bitset(EF_VRFYONLY, e->e_flags))
	{
		a->q_flags |= QVERIFIED;
		e->e_nrcpts++;
		return;
	}
	message("aliased to %s", p);
#ifdef LOG
	if (LogLevel > 9)
		syslog(LOG_INFO, "%s: alias %s => %s", e->e_id, a->q_paddr, p);
#endif
	a->q_flags &= ~QSELFREF;
	AliasLevel++;
	naliases = sendtolist(p, a, sendq, e);
	AliasLevel--;
	if (naliases > 0 && !bitset(QSELFREF, a->q_flags))
	{
		if (tTd(27, 5))
		{
			printf("alias: QDONTSEND ");
			printaddr(a, FALSE);
		}
		a->q_flags |= QDONTSEND;
	}

	/*
	**  Look for owner of alias
	*/

	(void) strcpy(obuf, "owner-");
	if (strncmp(a->q_user, "owner-", 6) == 0)
		(void) strcat(obuf, "owner");
	else
		(void) strcat(obuf, a->q_user);
	if (!bitnset(M_USR_UPPER, a->q_mailer->m_flags))
		makelower(obuf);
	owner = aliaslookup(obuf, e);
	if (owner != NULL)
	{
		if (strchr(owner, ',') != NULL)
			owner = obuf;
		a->q_owner = newstr(owner);
	}
}
/*
**  ALIASLOOKUP -- look up a name in the alias file.
**
**	Parameters:
**		name -- the name to look up.
**
**	Returns:
**		the value of name.
**		NULL if unknown.
**
**	Side Effects:
**		none.
**
**	Warnings:
**		The return value will be trashed across calls.
*/

char *
aliaslookup(name, e)
	char *name;
	ENVELOPE *e;
{
	register int dbno;
	register MAP *map;
	register char *p;

	for (dbno = 0; dbno < NAliasDBs; dbno++)
	{
		auto int stat;

		map = AliasDB[dbno];
		if (!bitset(MF_OPEN, map->map_mflags))
			continue;
		p = (*map->map_class->map_lookup)(map, name, NULL, &stat);
		if (p != NULL)
			return p;
	}
	return NULL;
}
/*
**  SETALIAS -- set up an alias map
**
**	Called when reading configuration file.
**
**	Parameters:
**		spec -- the alias specification
**
**	Returns:
**		none.
*/

setalias(spec)
	char *spec;
{
	register char *p;
	register MAP *map;
	char *class;
	STAB *s;

	if (tTd(27, 8))
		printf("setalias(%s)\n", spec);

	for (p = spec; p != NULL; )
	{
		char aname[50];

		while (isspace(*p))
			p++;
		if (*p == '\0')
			break;
		spec = p;

		if (NAliasDBs >= MAXALIASDB)
		{
			syserr("Too many alias databases defined, %d max", MAXALIASDB);
			return;
		}
		(void) sprintf(aname, "Alias%d", NAliasDBs);
		s = stab(aname, ST_MAP, ST_ENTER);
		map = &s->s_map;
		AliasDB[NAliasDBs] = map;
		bzero(map, sizeof *map);

		p = strpbrk(p, " ,/:");
		if (p != NULL && *p == ':')
		{
			/* map name */
			*p++ = '\0';
			class = spec;
			spec = p;
		}
		else
		{
			class = "implicit";
			map->map_mflags = MF_OPTIONAL|MF_INCLNULL;
		}

		/* find end of spec */
		if (p != NULL)
			p = strchr(p, ',');
		if (p != NULL)
			*p++ = '\0';

		/* look up class */
		s = stab(class, ST_MAPCLASS, ST_FIND);
		if (s == NULL)
		{
			if (tTd(27, 1))
				printf("Unknown alias class %s\n", class);
		}
		else if (!bitset(MCF_ALIASOK, s->s_mapclass.map_cflags))
		{
			syserr("setalias: map class %s can't handle aliases",
				class);
		}
		else
		{
			map->map_class = &s->s_mapclass;
			if (map->map_class->map_parse(map, spec))
			{
				map->map_mflags |= MF_VALID|MF_ALIAS;
				NAliasDBs++;
			}
		}
	}
}
/*
**  ALIASWAIT -- wait for distinguished @:@ token to appear.
**
**	This can decide to reopen or rebuild the alias file
*/

aliaswait(map, ext)
	MAP *map;
	char *ext;
{
	int atcnt;
	time_t mtime;
	struct stat stb;
	char buf[MAXNAME];

	if (tTd(27, 3))
		printf("aliaswait(%s:%s)\n",
			map->map_class->map_cname, map->map_file);

	atcnt = SafeAlias * 2;
	if (atcnt > 0)
	{
		auto int st;

		while (atcnt-- >= 0 &&
		       map->map_class->map_lookup(map, "@", NULL, &st) == NULL)
		{
			/*
			**  Close and re-open the alias database in case
			**  the one is mv'ed instead of cp'ed in.
			*/

			if (tTd(27, 2))
				printf("aliaswait: sleeping\n");

			map->map_class->map_close(map);
			sleep(30);
			map->map_class->map_open(map, O_RDONLY);
		}
	}

	/* see if we need to go into auto-rebuild mode */
	if (!bitset(MCF_REBUILDABLE, map->map_class->map_cflags))
	{
		if (tTd(27, 3))
			printf("aliaswait: not rebuildable\n");
		return;
	}
	if (stat(map->map_file, &stb) < 0)
	{
		if (tTd(27, 3))
			printf("aliaswait: no source file\n");
		return;
	}
	mtime = stb.st_mtime;
	(void) strcpy(buf, map->map_file);
	if (ext != NULL)
		(void) strcat(buf, ext);
	if (stat(buf, &stb) < 0 || stb.st_mtime < mtime || atcnt < 0)
	{
		/* database is out of date */
		if (AutoRebuild && stb.st_ino != 0 && stb.st_uid == geteuid())
		{
			message("auto-rebuilding alias database %s", buf);
			rebuildaliases(map, TRUE);
		}
		else
		{
#ifdef LOG
			if (LogLevel > 3)
				syslog(LOG_INFO, "alias database %s out of date",
					buf);
#endif /* LOG */
			message("Warning: alias database %s out of date", buf);
		}
	}
}
/*
**  REBUILDALIASES -- rebuild the alias database.
**
**	Parameters:
**		map -- the database to rebuild.
**		automatic -- set if this was automatically generated.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Reads the text version of the database, builds the
**		DBM or DB version.
*/

rebuildaliases(map, automatic)
	register MAP *map;
	bool automatic;
{
	FILE *af;
	void (*oldsigint)();

	if (!bitset(MCF_REBUILDABLE, map->map_class->map_cflags))
		return;

#ifdef LOG
	if (LogLevel > 7)
	{
		syslog(LOG_NOTICE, "alias database %s %srebuilt by %s",
			map->map_file, automatic ? "auto" : "", username());
	}
#endif /* LOG */

	/* try to lock the source file */
	if ((af = fopen(map->map_file, "r+")) == NULL)
	{
		if (tTd(27, 1))
			printf("Can't open %s: %s\n",
				map->map_file, errstring(errno));
		errno = 0;
		return;
	}

	/* see if someone else is rebuilding the alias file */
	if (!lockfile(fileno(af), map->map_file, LOCK_EX|LOCK_NB))
	{
		/* yes, they are -- wait until done */
		message("Alias file %s is already being rebuilt",
			map->map_file);
		if (OpMode != MD_INITALIAS)
		{
			/* wait for other rebuild to complete */
			(void) lockfile(fileno(af), map->map_file,
					LOCK_EX);
		}
		(void) fclose(af);
		errno = 0;
		return;
	}

	oldsigint = signal(SIGINT, SIG_IGN);

	if (map->map_class->map_open(map, O_RDWR))
	{
		map->map_mflags |= MF_OPEN|MF_WRITABLE;
		readaliases(map, af, automatic);
	}
	else
	{
		if (tTd(27, 1))
			printf("Can't create database for %s: %s\n",
				map->map_file, errstring(errno));
		if (!automatic)
			syserr("Cannot create database for alias file %s",
				map->map_file);
	}

	/* close the file, thus releasing locks */
	fclose(af);

	/* add distinguished entries and close the database */
	if (bitset(MF_OPEN, map->map_mflags))
		map->map_class->map_close(map);

	/* restore the old signal */
	(void) signal(SIGINT, oldsigint);
}
/*
**  READALIASES -- read and process the alias file.
**
**	This routine implements the part of initaliases that occurs
**	when we are not going to use the DBM stuff.
**
**	Parameters:
**		map -- the alias database descriptor.
**		af -- file to read the aliases from.
**		automatic -- set if this was an automatic rebuild.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Reads aliasfile into the symbol table.
**		Optionally, builds the .dir & .pag files.
*/

readaliases(map, af, automatic)
	register MAP *map;
	FILE *af;
	int automatic;
{
	register char *p;
	char *rhs;
	bool skipping;
	long naliases, bytes, longest;
	ADDRESS al, bl;
	char line[BUFSIZ];

	/*
	**  Read and interpret lines
	*/

	FileName = map->map_file;
	LineNumber = 0;
	naliases = bytes = longest = 0;
	skipping = FALSE;
	while (fgets(line, sizeof (line), af) != NULL)
	{
		int lhssize, rhssize;

		LineNumber++;
		p = strchr(line, '\n');
		if (p != NULL)
			*p = '\0';
		switch (line[0])
		{
		  case '#':
		  case '\0':
			skipping = FALSE;
			continue;

		  case ' ':
		  case '\t':
			if (!skipping)
				syserr("554 Non-continuation line starts with space");
			skipping = TRUE;
			continue;
		}
		skipping = FALSE;

		/*
		**  Process the LHS
		**	Find the colon separator, and parse the address.
		**	It should resolve to a local name -- this will
		**	be checked later (we want to optionally do
		**	parsing of the RHS first to maximize error
		**	detection).
		*/

		for (p = line; *p != '\0' && *p != ':' && *p != '\n'; p++)
			continue;
		if (*p++ != ':')
		{
			syserr("554 missing colon");
			continue;
		}
		if (parseaddr(line, &al, 1, ':', NULL, CurEnv) == NULL)
		{
			syserr("554 illegal alias name");
			continue;
		}

		/*
		**  Process the RHS.
		**	'al' is the internal form of the LHS address.
		**	'p' points to the text of the RHS.
		*/

		while (isascii(*p) && isspace(*p))
			p++;
		rhs = p;
		for (;;)
		{
			register char c;
			register char *nlp;

			nlp = &p[strlen(p)];
			if (nlp[-1] == '\n')
				*--nlp = '\0';

			if (CheckAliases)
			{
				/* do parsing & compression of addresses */
				while (*p != '\0')
				{
					auto char *delimptr;

					while ((isascii(*p) && isspace(*p)) ||
								*p == ',')
						p++;
					if (*p == '\0')
						break;
					if (parseaddr(p, &bl, -1, ',', &delimptr, CurEnv) == NULL)
						usrerr("553 %s... bad address", p);
					p = delimptr;
				}
			}
			else
			{
				p = nlp;
			}

			/* see if there should be a continuation line */
			c = fgetc(af);
			if (!feof(af))
				(void) ungetc(c, af);
			if (c != ' ' && c != '\t')
				break;

			/* read continuation line */
			if (fgets(p, sizeof line - (p - line), af) == NULL)
				break;
			LineNumber++;

			/* check for line overflow */
			if (strchr(p, '\n') == NULL)
			{
				usrerr("554 alias too long");
				break;
			}
		}
		if (al.q_mailer != LocalMailer)
		{
			syserr("554 cannot alias non-local names");
			continue;
		}

		/*
		**  Insert alias into symbol table or DBM file
		*/

		if (!bitnset(M_USR_UPPER, al.q_mailer->m_flags))
			makelower(al.q_user);

		lhssize = strlen(al.q_user);
		rhssize = strlen(rhs);
		map->map_class->map_store(map, al.q_user, rhs);

		if (al.q_paddr != NULL)
			free(al.q_paddr);
		if (al.q_host != NULL)
			free(al.q_host);
		if (al.q_user != NULL)
			free(al.q_user);

		/* statistics */
		naliases++;
		bytes += lhssize + rhssize;
		if (rhssize > longest)
			longest = rhssize;
	}

	CurEnv->e_to = NULL;
	FileName = NULL;
	if (Verbose || !automatic)
		message("%s: %d aliases, longest %d bytes, %d bytes total",
			map->map_file, naliases, longest, bytes);
# ifdef LOG
	if (LogLevel > 7)
		syslog(LOG_INFO, "%s: %d aliases, longest %d bytes, %d bytes total",
			map->map_file, naliases, longest, bytes);
# endif /* LOG */
}
/*
**  FORWARD -- Try to forward mail
**
**	This is similar but not identical to aliasing.
**
**	Parameters:
**		user -- the name of the user who's mail we would like
**			to forward to.  It must have been verified --
**			i.e., the q_home field must have been filled
**			in.
**		sendq -- a pointer to the head of the send queue to
**			put this user's aliases in.
**
**	Returns:
**		none.
**
**	Side Effects:
**		New names are added to send queues.
*/

forward(user, sendq, e)
	ADDRESS *user;
	ADDRESS **sendq;
	register ENVELOPE *e;
{
	char *pp;
	char *ep;

	if (tTd(27, 1))
		printf("forward(%s)\n", user->q_paddr);

	if (user->q_mailer != LocalMailer || bitset(QBADADDR, user->q_flags))
		return;
	if (user->q_home == NULL)
	{
		syserr("554 forward: no home");
		user->q_home = "/nosuchdirectory";
	}

	/* good address -- look for .forward file in home */
	define('z', user->q_home, e);
	define('u', user->q_user, e);
	define('h', user->q_host, e);
	if (ForwardPath == NULL)
		ForwardPath = newstr("\201z/.forward");

	for (pp = ForwardPath; pp != NULL; pp = ep)
	{
		int err;
		char buf[MAXPATHLEN+1];

		ep = strchr(pp, ':');
		if (ep != NULL)
			*ep = '\0';
		expand(pp, buf, &buf[sizeof buf - 1], e);
		if (ep != NULL)
			*ep++ = ':';
		if (tTd(27, 3))
			printf("forward: trying %s\n", buf);
		err = include(buf, TRUE, user, sendq, e);
		if (err == 0)
			break;
		if (transienterror(err))
		{
			/* we have to suspend this message */
			if (tTd(27, 2))
				printf("forward: transient error on %s\n", buf);
#ifdef LOG
			if (LogLevel > 2)
				syslog(LOG_ERR, "%s: forward %s: transient error: %s",
					e->e_id, buf, errstring(err));
#endif
			message("%s: %s: message queued", buf, errstring(err));
			user->q_flags |= QQUEUEUP|QDONTSEND;
			return;
		}
	}
}
