/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "@(#)time.c	5.10 (Berkeley) 06/07/91";
#endif /* not lint */

#include "csh.h"
#include "extern.h"

/*
 * C Shell - routines handling process timing and niceing
 */
static void pdeltat();

void
settimes()
{
    struct rusage ruch;

    (void) gettimeofday(&time0, (struct timezone *) 0);
    (void) getrusage(RUSAGE_SELF, &ru0);
    (void) getrusage(RUSAGE_CHILDREN, &ruch);
    ruadd(&ru0, &ruch);
}

/*
 * dotime is only called if it is truly a builtin function and not a
 * prefix to another command
 */
void
dotime()
{
    struct timeval timedol;
    struct rusage ru1, ruch;

    (void) getrusage(RUSAGE_SELF, &ru1);
    (void) getrusage(RUSAGE_CHILDREN, &ruch);
    ruadd(&ru1, &ruch);
    (void) gettimeofday(&timedol, (struct timezone *) 0);
    prusage(&ru0, &ru1, &timedol, &time0);
}

/*
 * donice is only called when it on the line by itself or with a +- value
 */
void
donice(v)
    register Char **v;
{
    register Char *cp;
    int     nval = 0;

    v++, cp = *v++;
    if (cp == 0)
	nval = 4;
    else if (*v == 0 && any("+-", cp[0]))
	nval = getn(cp);
    (void) setpriority(PRIO_PROCESS, 0, nval);
}

void
ruadd(ru, ru2)
    register struct rusage *ru, *ru2;
{
    tvadd(&ru->ru_utime, &ru2->ru_utime);
    tvadd(&ru->ru_stime, &ru2->ru_stime);
    if (ru2->ru_maxrss > ru->ru_maxrss)
	ru->ru_maxrss = ru2->ru_maxrss;

    ru->ru_ixrss += ru2->ru_ixrss;
    ru->ru_idrss += ru2->ru_idrss;
    ru->ru_isrss += ru2->ru_isrss;
    ru->ru_minflt += ru2->ru_minflt;
    ru->ru_majflt += ru2->ru_majflt;
    ru->ru_nswap += ru2->ru_nswap;
    ru->ru_inblock += ru2->ru_inblock;
    ru->ru_oublock += ru2->ru_oublock;
    ru->ru_msgsnd += ru2->ru_msgsnd;
    ru->ru_msgrcv += ru2->ru_msgrcv;
    ru->ru_nsignals += ru2->ru_nsignals;
    ru->ru_nvcsw += ru2->ru_nvcsw;
    ru->ru_nivcsw += ru2->ru_nivcsw;
}

void
prusage(r0, r1, e, b)
    register struct rusage *r0, *r1;
    struct timeval *e, *b;
{
    register time_t t =
    (r1->ru_utime.tv_sec - r0->ru_utime.tv_sec) * 100 +
    (r1->ru_utime.tv_usec - r0->ru_utime.tv_usec) / 10000 +
    (r1->ru_stime.tv_sec - r0->ru_stime.tv_sec) * 100 +
    (r1->ru_stime.tv_usec - r0->ru_stime.tv_usec) / 10000;
    register char *cp;
    register long i;
    register struct varent *vp = adrof(STRtime);

    int     ms =
    (e->tv_sec - b->tv_sec) * 100 + (e->tv_usec - b->tv_usec) / 10000;

    cp = "%Uu %Ss %E %P %X+%Dk %I+%Oio %Fpf+%Ww";
#ifdef TDEBUG
    xprintf("es->tms_utime %lu bs->tms_utime %lu\n",
	    es->tms_utime, bs->tms_utime);
    xprintf("es->tms_stime %lu bs->tms_stime %lu\n",
	    es->tms_stime, bs->tms_stime);
    xprintf("ms %lu e %lu b %lu\n", ms, e, b);
    xprintf("t %lu\n", t);
#endif				/* TDEBUG */

    if (vp && vp->vec[0] && vp->vec[1])
	cp = short2str(vp->vec[1]);
    for (; *cp; cp++)
	if (*cp != '%')
	    xputchar(*cp);
	else if (cp[1])
	    switch (*++cp) {

	    case 'U':		/* user CPU time used */
		pdeltat(&r1->ru_utime, &r0->ru_utime);
		break;

	    case 'S':		/* system CPU time used */
		pdeltat(&r1->ru_stime, &r0->ru_stime);
		break;

	    case 'E':		/* elapsed (wall-clock) time */
		pcsecs((long) ms);
		break;

	    case 'P':		/* percent time spent running */
		i = (int) (t * 1000 / ((ms ? ms : 1)));
		xprintf("%ld.%01ld%%", i / 10, i % 10);	/* nn.n% */
		break;

	    case 'W':		/* number of swaps */
		i = r1->ru_nswap - r0->ru_nswap;
		xprintf("%ld", i);
		break;

	    case 'X':		/* (average) shared text size */
		xprintf("%ld", t == 0 ? 0L : (r1->ru_ixrss - r0->ru_ixrss) / t);
		break;

	    case 'D':		/* (average) unshared data size */
		xprintf("%ld", t == 0 ? 0L :
			(r1->ru_idrss + r1->ru_isrss -
			 (r0->ru_idrss + r0->ru_isrss)) / t);
		break;

	    case 'K':		/* (average) total data memory used  */
		xprintf("%ld", t == 0 ? 0L :
			((r1->ru_ixrss + r1->ru_isrss + r1->ru_idrss) -
			 (r0->ru_ixrss + r0->ru_idrss + r0->ru_isrss)) / t);
		break;

	    case 'M':		/* max. Resident Set Size */
		xprintf("%ld", r1->ru_maxrss / 2L);
		break;

	    case 'F':		/* page faults */
		xprintf("%ld", r1->ru_majflt - r0->ru_majflt);
		break;

	    case 'R':		/* page reclaims */
		xprintf("%ld", r1->ru_minflt - r0->ru_minflt);
		break;

	    case 'I':		/* FS blocks in */
		xprintf("%ld", r1->ru_inblock - r0->ru_inblock);
		break;

	    case 'O':		/* FS blocks out */
		xprintf("%ld", r1->ru_oublock - r0->ru_oublock);
		break;

	    case 'r':		/* socket messages recieved */
		xprintf("%ld", r1->ru_msgrcv - r0->ru_msgrcv);
		break;

	    case 's':		/* socket messages sent */
		xprintf("%ld", r1->ru_msgsnd - r0->ru_msgsnd);
		break;

	    case 'k':		/* number of signals recieved */
		xprintf("%ld", r1->ru_nsignals - r0->ru_nsignals);
		break;

	    case 'w':		/* num. voluntary context switches (waits) */
		xprintf("%ld", r1->ru_nvcsw - r0->ru_nvcsw);
		break;

	    case 'c':		/* num. involuntary context switches */
		xprintf("%ld", r1->ru_nivcsw - r0->ru_nivcsw);
		break;
	    }
    xputchar('\n');
}

static void
pdeltat(t1, t0)
    struct timeval *t1, *t0;
{
    struct timeval td;

    tvsub(&td, t1, t0);
    xprintf("%d.%01d", td.tv_sec, td.tv_usec / 100000);
}

void
tvadd(tsum, t0)
    struct timeval *tsum, *t0;
{

    tsum->tv_sec += t0->tv_sec;
    tsum->tv_usec += t0->tv_usec;
    if (tsum->tv_usec > 1000000)
	tsum->tv_sec++, tsum->tv_usec -= 1000000;
}

void
tvsub(tdiff, t1, t0)
    struct timeval *tdiff, *t1, *t0;
{

    tdiff->tv_sec = t1->tv_sec - t0->tv_sec;
    tdiff->tv_usec = t1->tv_usec - t0->tv_usec;
    if (tdiff->tv_usec < 0)
	tdiff->tv_sec--, tdiff->tv_usec += 1000000;
}
