/*
 * Copyright (c) 1987, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "@(#)sincos.c	8.1 (Berkeley) 06/04/93";
#endif /* not lint */

#include "trig.h"
double
sin(x)
double x;
{
	double a,c,z;

        if(!finite(x))		/* sin(NaN) and sin(INF) must be NaN */
		return x-x;
	x=drem(x,PI2);		/* reduce x into [-PI,PI] */
	a=copysign(x,one);
	if (a >= PIo4) {
		if(a >= PI3o4)		/* ... in [3PI/4,PI] */
			x = copysign((a = PI-a),x);
		else {			/* ... in [PI/4,3PI/4]  */
			a = PIo2-a;		/* rtn. sign(x)*C(PI/2-|x|) */
			z = a*a;
			c = cos__C(z);
			z *= half;
			a = (z >= thresh ? half-((z-half)-c) : one-(z-c));
			return copysign(a,x);
		}
	}

	if (a < small) {		/* rtn. S(x) */
		big+a;
		return x;
	}
	return x+x*sin__S(x*x);
}

double
cos(x) 
double x;
{
	double a,c,z,s = 1.0;

	if(!finite(x))		/* cos(NaN) and cos(INF) must be NaN */
		return x-x;
	x=drem(x,PI2);		/* reduce x into [-PI,PI] */
	a=copysign(x,one);
	if (a >= PIo4) {
		if (a >= PI3o4) {	/* ... in [3PI/4,PI] */
			a = PI-a;
			s = negone;
		}
		else {			/* ... in [PI/4,3PI/4] */
			a = PIo2-a;
			return a+a*sin__S(a*a);	/* rtn. S(PI/2-|x|) */ 
		}
	}
	if (a < small) {
		big+a;
		return s;		/* rtn. s*C(a) */
	}
	z = a*a;
	c = cos__C(z);
	z *= half;
	a = (z >= thresh ? half-((z-half)-c) : one-(z-c));
	return copysign(a,s);
}
