/*-
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char sccsid[] = "@(#)des_rw.c	8.1 (Berkeley) 06/06/93";
#endif /* not lint */

#ifdef CRYPT
#ifdef KERBEROS
#include <sys/param.h>

#include <kerberosIV/des.h>
#include <kerberosIV/krb.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static unsigned char	des_inbuf[10240], storage[10240], *store_ptr;
static bit_64		*key;
static u_char		*key_schedule;

/* XXX these should be in a kerberos include file */
int	krb_net_read __P((int, char *, int));
#ifdef notdef
/* XXX too hard to make this work */
int	des_pcbc_encrypt __P((des_cblock *, des_cblock *, long,
	    des_key_schedule, des_cblock *, int));
#endif

/*
 * NB: These routines will not function properly if NBIO
 * 	is set
 */

/*
 * des_set_key
 *
 * Set des encryption/decryption key for use by the des_read and
 * des_write routines
 *
 * The inkey parameter is actually the DES initial vector,
 * and the insched is the DES Key unwrapped for faster decryption
 */

void
des_set_key(inkey, insched)
	bit_64		*inkey;
	u_char		*insched;
{
	key = inkey;
	key_schedule = insched;
}

void
des_clear_key()
{
	bzero((char *) key, sizeof(C_Block));
	bzero((char *) key_schedule, sizeof(Key_schedule));
}
	

int
des_read(fd, buf, len)
	int fd;
	register char *buf;
	int len;
{
	int nreturned = 0;
	long net_len, rd_len;
	int nstored = 0;

	if (nstored >= len) {
		(void) bcopy(store_ptr, buf, len);
		store_ptr += len;
		nstored -= len;
		return(len);
	} else if (nstored) {
		(void) bcopy(store_ptr, buf, nstored);
		nreturned += nstored;
		buf += nstored;
		len -= nstored;
		nstored = 0;
	}
	
	if (krb_net_read(fd, (char *)&net_len, sizeof(net_len)) !=
	    sizeof(net_len)) {
		/* XXX can't read enough, pipe
		   must have closed */
		return(0);
	}
	net_len = ntohl(net_len);
	if (net_len <= 0 || net_len > sizeof(des_inbuf)) {
		/* preposterous length; assume out-of-sync; only
		   recourse is to close connection, so return 0 */
		return(0);
	}
	/* the writer tells us how much real data we are getting, but
	   we need to read the pad bytes (8-byte boundary) */
	rd_len = roundup(net_len, 8);
	if (krb_net_read(fd, (char *)des_inbuf, rd_len) != rd_len) {
		/* pipe must have closed, return 0 */
		return(0);
	}
	(void) des_pcbc_encrypt(des_inbuf,	/* inbuf */
			    storage,		/* outbuf */
			    net_len,		/* length */
			    key_schedule,	/* DES key */
			    key,		/* IV */
			    DECRYPT);		/* direction */

	if(net_len < 8)
		store_ptr = storage + 8 - net_len;
	else
		store_ptr = storage;

	nstored = net_len;
	if (nstored > len) {
		(void) bcopy(store_ptr, buf, len);
		nreturned += len;
		store_ptr += len;
		nstored -= len;
	} else {
		(void) bcopy(store_ptr, buf, nstored);
		nreturned += nstored;
		nstored = 0;
	}
	
	return(nreturned);
}

static	unsigned char des_outbuf[10240];	/* > longest write */

int
des_write(fd, buf, len)
	int fd;
	char *buf;
	int len;
{
	static	int	seeded = 0;
	static	char	garbage_buf[8];
	long net_len, garbage;

	if(len < 8) {
		if(!seeded) {
			seeded = 1;
			srandom((int) time((long *)0));
		}
		garbage = random();
		/* insert random garbage */
		(void) bcopy(&garbage, garbage_buf, MIN(sizeof(long),8));
		/* this "right-justifies" the data in the buffer */
		(void) bcopy(buf, garbage_buf + 8 - len, len);
	}
	/* pcbc_encrypt outputs in 8-byte (64 bit) increments */

	(void) des_pcbc_encrypt((len < 8) ? garbage_buf : buf,
			    des_outbuf,
			    (len < 8) ? 8 : len,
			    key_schedule,	/* DES key */
			    key,		/* IV */
			    ENCRYPT);

	/* tell the other end the real amount, but send an 8-byte padded
	   packet */
	net_len = htonl(len);
	(void) write(fd, &net_len, sizeof(net_len));
	(void) write(fd, des_outbuf, roundup(len,8));
	return(len);
}
#endif /* KERBEROS */
#endif /* CRYPT */
