/*	ut.c	4.1	81/11/04	*/

#include "ut.h"
#if NUT > 0
#define UTDEBUG	1
/*
 * System Industries Model 9700 Tape Drive
 *   emulates a TU45 on the UNIBUS
 *
 * TODO:
 *	check out attention processing
 *	try reset code and dump code
 */
#include "../h/param.h"
#include "../h/systm.h"
#include "../h/buf.h"
#include "../h/conf.h"
#include "../h/dir.h"
#include "../h/file.h"
#include "../h/user.h"
#include "../h/map.h"
#include "../h/pte.h"
#include "../h/ubareg.h"
#include "../h/ubavar.h"
#include "../h/mtio.h"
#include "../h/ioctl.h"
#include "../h/cmap.h"
#include "../h/cpu.h"

#include "../h/utreg.h"

struct	buf	rutbuf[NUT];	/* bufs for raw i/o */
struct	buf	cutbuf[NUT];	/* bufs for control operations */
struct	buf	tjutab[NTJ];	/* bufs for slave queue headers */

struct uba_ctlr *utminfo[NUT];
struct uba_device *tjdinfo[NTJ];
int utprobe(), utslave(), utattach(), utdgo();
u_short utstd[] = { 0772440, 0 };
struct uba_driver utdriver =
  { utprobe, utslave, utattach, utdgo, utstd, "tj", tjdinfo, "ut", utminfo, 0 };

/* bits in minor device */
#define	TJUNIT(dev)	(minor(dev)&03)
#define	T_NOREWIND	04
#define	T_1600BPI	010
#define	T_6250BPI	020
short	utdens[] = { UT_NRZI, UT_PE, UT_GCR, UT_NRZI };

/* slave to controller mapping table */
short	tjtout[NTJ];
#define UTUNIT(dev)	(tjtout[TJUNIT(dev)])

#define	INF	(daddr_t)1000000L	/* a block number that wont exist */

struct	tj_softc {
	char	sc_openf;	/* exclusive open */
	char	sc_lastiow;	/* last I/O operation was a write */
	daddr_t	sc_blkno;	/* next block to transfer */
	daddr_t	sc_nxrec;	/* next record on tape */
	u_short	sc_erreg;	/* image of uter */
	u_short	sc_dsreg;	/* image of utds */
	short	sc_resid;	/* residual from transfer */
	u_short	sc_dens;	/* sticky selected density */
} tj_softc[NTJ];

/*
 * Internal per/slave states found in sc_state
 */
#define	SSEEK		1	/* seeking */
#define	SIO		2	/* doing sequential I/O */
#define	SCOM		3	/* sending a control command */
#define	SREW		4	/* doing a rewind op */

#if UTDEBUG
int	utdebug;
#define printd	if (utdebug) printf
#else
#define	printd
#endif

/*
 * A NOP should get an interrupt back, if the
 *  device is there.
 */
utprobe(reg)
	caddr_t reg;
{
	register int br, cvec;
#ifdef lint
	br=0; cvec=br; br=cvec;
#endif
	((struct utdevice *) reg)->utcs1 = UT_IE|UT_NOP|UT_GO;
	DELAY(10000);
	((struct utdevice *) reg)->utcs1 = UT_CLEAR|UT_GO;
	return(1);
}

/*ARGSUSED*/
utslave(ui, reg)
	struct uba_device *ui;
	caddr_t reg;
{
	/*
	 * A real TU45 would support the slave present bit
	 * int the drive type register, but this thing doesn't,
	 * so there's no way to determine if a slave is present or not.
	 */
	 return(1);
}

utattach(ui)
	struct uba_device *ui;
{
	tjtout[ui->ui_unit] = ui->ui_mi->um_ctlr;
}

/*
 * Open the device with exclusive access.
 */
utopen(dev, flag)
	dev_t dev;
	int flag;
{
	register int tjunit = TJUNIT(dev);
	register struct uba_device *ui;
	register struct tj_softc *sc;
	int olddens, dens;

	if (tjunit >= NTJ || (sc = &tj_softc[tjunit])->sc_openf ||
	    (ui = tjdinfo[tjunit]) == 0 || ui->ui_alive == 0) {
		u.u_error = ENXIO;
		return;
	}
	olddens = sc->sc_dens;
	dens = sc->sc_dens = utdens[(minor(dev)&(T_1600BPI|T_6250BPI))>>3]|
				PDP11FMT|(ui->ui_slave&07);
get:
	utcommand(dev, UT_SENSE, 1);
	if (sc->sc_dsreg&UTDS_PIP) {
		sleep((caddr_t) &lbolt, PZERO+1);
		goto get;
	}
	sc->sc_dens = olddens;
	if ((sc->sc_dsreg&UTDS_MOL) == 0) {
		uprintf("tj%d: not online\n", tjunit);
		u.u_error = EIO;
		return;
	}
	if ((flag&FWRITE) && (sc->sc_dsreg&UTDS_WRL)) {
		uprintf("tj%d: no write ring\n", tjunit);
		u.u_error = EIO;
		return;
	}
	if ((sc->sc_dsreg&UTDS_BOT) == 0 && (flag&FWRITE) &&
	    dens != sc->sc_dens) {
		uprintf("tj%d: can't change density in mid-tape\n", tjunit);
		u.u_error = EIO;
		return;
	}
	sc->sc_openf = 1;
	sc->sc_blkno = (daddr_t)0;
	sc->sc_nxrec = INF;
	sc->sc_lastiow = 0;
	sc->sc_dens = dens;
}

utclose(dev, flag)
	register dev_t dev;
	register flag;
{
	register struct tj_softc *sc = &tj_softc[TJUNIT(dev)];

	if (flag == FWRITE || ((flag&FWRITE) && sc->sc_lastiow)) {
		utcommand(dev, UT_WEOF, 1);
		utcommand(dev, UT_WEOF, 1);
		utcommand(dev, UT_SREV, 1);
	}
	if ((minor(dev)&T_NOREWIND) == 0)
		utcommand(dev, UT_REW, 0);
	sc->sc_openf = 0;
}

utcommand(dev, com, count)
	dev_t dev;
	int com, count;
{
	register struct buf *bp;

	bp = &cutbuf[UTUNIT(dev)];
	(void) spl5();
	while (bp->b_flags&B_BUSY) {
		if(bp->b_repcnt == 0 && (bp->b_flags&B_DONE))
			break;
		bp->b_flags |= B_WANTED;
		sleep((caddr_t)bp, PRIBIO);
	}
	bp->b_flags = B_BUSY|B_READ;
	(void) spl0();
	bp->b_dev = dev;
	bp->b_command = com;
	bp->b_repcnt = count;
	bp->b_blkno = 0;
	utstrategy(bp);
	if (count == 0)
		return;
	iowait(bp);
	if (bp->b_flags&B_WANTED)
		wakeup((caddr_t)bp);
	bp->b_flags &= B_ERROR;
}

/*
 * Queue a tape operation.
 */
utstrategy(bp)
	register struct buf *bp;
{
	int tjunit = TJUNIT(bp->b_dev);
	register struct uba_ctlr *um;
	register struct buf *dp;

	/*
	 * Put transfer at end of unit queue
	 */
	dp = &tjutab[tjunit];
	bp->av_forw = NULL;
	(void) spl5();
	if (dp->b_actf == NULL) {
		dp->b_actf = bp;
		/*
		 * Transport not active, so...
		 * put at end of controller queue
		 */
		dp->b_forw = NULL;
		um = tjdinfo[tjunit]->ui_mi;
		if (um->um_tab.b_actf == NULL)
			um->um_tab.b_actf = dp;
		else
			um->um_tab.b_actl->b_forw = dp;
		um->um_tab.b_actl = dp;
	} else
		dp->b_actl->av_forw = bp;
	dp->b_actl = bp;
	/*
	 * If the controller is not busy, set it going.
	 */
	if (um->um_tab.b_active == 0)
		utstart(um);
	(void) spl0();
}

utstart(um)
	register struct uba_ctlr *um;
{
	register struct utdevice *utaddr;
	register struct buf *bp, *dp;
	register struct tj_softc *sc;
	struct uba_device *ui;
	int tjunit;
	daddr_t blkno;

loop:
	/*
	 * Scan controller queue looking for units with
	 * transaction queues to dispatch
	 */
	if ((dp = um->um_tab.b_actf) == NULL)
		return;
	if ((bp = dp->b_actf) == NULL) {
		um->um_tab.b_actf = dp->b_forw;
		goto loop;
	}
	utaddr = (struct utdevice *)um->um_addr;
	tjunit = TJUNIT(bp->b_dev);
	ui = tjdinfo[tjunit];
	sc = &tj_softc[tjunit];
	/* note slave select, density, and format were merged on open */
	utaddr->uttc = sc->sc_dens;
	sc->sc_dsreg = utaddr->utds;
	sc->sc_erreg = utaddr->uter;
	sc->sc_resid = utaddr->utwc;
	/*
	 * Default is that last command was NOT a write command;
	 * if we do a write command we will notice this in utintr().
	 */
	sc->sc_lastiow = 0;
	printd("utstart: openf=%d ds=%b\n", sc->sc_openf, utaddr->utds,
		UTDS_BITS);
	if (sc->sc_openf < 0 || (utaddr->utds&UTDS_MOL) == 0) {
		/*
		 * Have had a hard error on a non-raw tape
		 * or the tape unit is now unavailable
		 * (e.g. taken off line).
		 */
		bp->b_flags |= B_ERROR;
		goto next;
	}
	if (bp == &cutbuf[UTUNIT(bp->b_dev)]) {
		/*
		 * Execute a control operation with the specified
		 * count.
		 */
		if (bp->b_command == UT_SENSE)
			goto next;
		/*
		 * Set next state; handle timeouts
		 */
		if (bp->b_command == UT_REW)
			um->um_tab.b_active = SREW;
		else
			um->um_tab.b_active = SCOM;
		/* NOTE: this depends on the ut command values */
		if (bp->b_command >= UT_SFORW && bp->b_command <= UT_SREVF)
			utaddr->utfc = bp->b_repcnt;
		goto dobpcmd;
	}
	/*
	 * The following checks boundary conditions for operations
	 * on non-raw tapes.  On raw tapes the initialization of
	 * sc->sc_nxrec by utphys causes them to be skipped normally
	 * (except in the case of retries).
	 */
	if (dbtofsb(bp->b_blkno) > sc->sc_nxrec) {
		/* can't read past end of file */
		bp->b_flags |= B_ERROR;
		bp->b_error = ENXIO;
		goto next;
	}
	if (dbtofsb(bp->b_blkno) == sc->sc_nxrec && (bp->b_flags&B_READ)) {
		/* read at eof returns 0 count */
		bp->b_resid = bp->b_bcount;
		clrbuf(bp);
		goto next;
	}
	if ((bp->b_flags&B_READ) == 0)
		sc->sc_nxrec = dbtofsb(bp->b_blkno)+1;
	/*
	 * If the tape is correctly positioned, set up all the
	 * registers but the csr, and give control over to the
	 * UNIBUS adaptor routines, to wait for resources to
	 * start I/O.
	 */
	if ((blkno = sc->sc_blkno) == dbtofsb(bp->b_blkno)) {
		utaddr->utwc = -(((bp->b_bcount)+1)>>1);
		if ((bp->b_flags&B_READ) == 0) {
			/*
			 * On write error retries erase the
			 * inter-record gap
			 */
			if (um->um_tab.b_errcnt)
				um->um_cmd = UT_ERASE;
			else
				um->um_cmd = UT_WCOM;
		} else
			um->um_cmd = UT_RCOM;
		um->um_tab.b_active = SIO;
		(void) ubago(ui);
		return;
	}
	/*
	 * Tape positioned incorrectly; seek forwards or
	 * backwards to the correct spot.  This happens for
	 * raw tapes only on error retries.
	 */
	printd("utstart: seek, blkno=%d dbtofsb=%d\n", blkno,
		dbtofsb(bp->b_blkno));
	um->um_tab.b_active = SSEEK;
	if (blkno < dbtofsb(bp->b_blkno)) {
		utaddr->utfc = blkno - dbtofsb(bp->b_blkno);
		bp->b_command = UT_SFORW;
	} else {
		utaddr->utfc = dbtofsb(bp->b_blkno) - blkno;
		bp->b_command = UT_SREV;
	}

dobpcmd:
	/*
	 * Perform the command setup in bp.
	 */
	printd("utstart: dobpcmd\n");
	utaddr->utcs1 = bp->b_command|UT_IE|UT_GO;
	return;
next:
	/*
	 * Advance to the next command in the slave queue,
	 * posting notice and releasing resources as needed.
	 */
	printd("utstart: next\n");
	if (um->um_ubinfo)
		ubadone(um);
	um->um_tab.b_errcnt = 0;
	dp->b_actf = bp->av_forw;
	iodone(bp);
	goto loop;
}

/*
 * Start operation on controller --
 * UNIBUS resources have been allocated.
 */
utdgo(um)
	register struct uba_ctlr *um;
{
	register struct utdevice *addr = (struct utdevice *)um->um_addr;

	addr->utba = (u_short) um->um_ubinfo;
	addr->utcs1 = um->um_cmd|((um->um_ubinfo>>8)&0x30)|UT_IE|UT_GO;
	printd("utdgo: cs1=%b\n", addr->utcs1, UT_BITS);
}

/*
 * Ut interrupt handler
 */
/*ARGSUSED*/
utintr(ut11)
	int ut11;
{
	struct buf *dp;
	register struct buf *bp;
	register struct uba_ctlr *um = utminfo[ut11];
	register struct utdevice *addr;
	register struct tj_softc *sc;
	int tjunit;
	register state;

	if ((dp = um->um_tab.b_actf) == NULL)
		return;
	bp = dp->b_actf;
	tjunit = TJUNIT(bp->b_dev);
	addr = (struct utdevice *)tjdinfo[tjunit]->ui_addr;
	sc = &tj_softc[tjunit];
	/*
	 * Record status...
	 */
	sc->sc_dsreg = addr->utds;
	sc->sc_erreg = addr->uter;
	sc->sc_resid = addr->utwc;
	printd("utintr: state=%d cs1=%b cs2=%b ds=%b er=%b\n",
		um->um_tab.b_active,
		((struct utdevice *) addr)->utcs1, UT_BITS,
		((struct utdevice *) addr)->utcs2, UTCS2_BITS,
		((struct utdevice *) addr)->utds, UTDS_BITS,
		((struct utdevice *) addr)->uter, UTER_BITS);
	/*
	 * Check for stray attentions from slaves going online, offline,
	 * or a completing rewind.  (The rewind started interrupt
	 * satisfied the requestor of the rewind.)
	 */
	if (((addr->utcs1&(UT_SC|UT_TRE)) == UT_SC) &&
	    (addr->utds&UTDS_ERR) == 0) {
		addr->utas = 0xff;	/* writing a 1 clears attention */
		/*
		 * If we're doing a rewind and we're at the beginning
		 * of tape, then the attention and the rewind 
		 * command may complete at the same time -- resulting in only
		 * one interrupt.  In this case, simulate things to look like
		 * the attention was the command complete.
		 */
		if (bp->b_command != UT_REW && bp->b_command != UT_REWOFFL)
			return;
		if ((addr->utds&UTDS_BOT) == 0)
			return;
		um->um_tab.b_active = SCOM;
	}
	if((bp->b_flags&B_READ) == 0)
		sc->sc_lastiow = 1;
	state = um->um_tab.b_active;
	um->um_tab.b_active = 0;
	/*
	 * Check for errors...
	 */
	if ((addr->utds&UTDS_ERR) || (addr->utcs1&UT_TRE)) {
#ifdef notdef
		/*
		 * if this bit were emulated, it would allow us to wait
		 * for the transport to settle
		 */
		while (addr->utds&UTDS_SDWN)
			;
#endif
		/*
		 * If we hit the end of tape, update our position
		 */
		if (addr->utds&UTDS_EOT) {
			/*
			 * Set blkno and nxrec on sensing end of tape.
			 */
			if (bp == &cutbuf[UTUNIT(bp->b_dev)]) {
				if (sc->sc_blkno > dbtofsb(bp->b_blkno)) {
					/* reversing */
					sc->sc_nxrec =
					      dbtofsb(bp->b_blkno) - addr->utfc;
					sc->sc_blkno = sc->sc_nxrec;
				} else {
					/* spacing forward */
					sc->sc_blkno =
					      dbtofsb(bp->b_blkno) + addr->utfc;
					sc->sc_nxrec = sc->sc_blkno-1;
				}
			} else		/* eof on read */
				sc->sc_nxrec = dbtofsb(bp->b_blkno);
			state = SCOM;		/* force completion */
			addr->utcs1 = UT_CLEAR|UT_GO;
			/*
			 * Stuff fc so that it will be unstuffed correctly
			 * later to get the residual.
			 */
			addr->utfc = -bp->b_bcount;
			goto opdone;
		}
		addr->utcs1 = UT_CLEAR|UT_GO;	/* must clear ERR bit */
		/*
		 * If we were reading from a raw tape and the only error
		 * was that the record was too long, then we don't consider
		 * this an error.
		 */
		if (bp == &rutbuf[UTUNIT(bp->b_dev)] && (bp->b_flags&B_READ) &&
		    (sc->sc_erreg&UTER_FCE))
			goto ignoreerr;
		/*
		 * Retry soft errors up to 8 times
		 */
		if ((sc->sc_erreg&UTER_HARD) == 0 && state == SIO) {
			if (++um->um_tab.b_errcnt < 7) {
				sc->sc_blkno++;
				ubadone(um);
				goto opcont;
			}
		} else
			/*
			 * Hard or non-I/O errors on non-raw tape
			 * cause it to close.
			 */
			if (sc->sc_openf > 0 && bp != &rutbuf[UTUNIT(bp->b_dev)])
				sc->sc_openf = -1;
		/*
		 * Couldn't recover error.
		 */
		printf("ut%d: hard error bn%d er=%b cs2=%b\n", tjunit,
			bp->b_blkno, sc->sc_erreg, UTER_BITS,
			addr->utcs2, UTCS2_BITS);
		bp->b_flags |= B_ERROR;
		goto opdone;
	}
ignoreerr:
	/*
	 * Advance tape control FSM.
	 */
	switch (state) {

	case SIO:		/* read/write increments tape block # */
		sc->sc_blkno++;
		goto opdone;

	case SCOM:		/* forw/rev space updates current position */
		if (bp == &cutbuf[UTUNIT(bp->b_dev)])
		switch (bp->b_command) {

		case UT_SFORW:
			sc->sc_blkno -= bp->b_repcnt;
			break;

		case UT_SREV:
			sc->sc_blkno += bp->b_repcnt;
			break;
		}
		goto opdone;

	case SSEEK:
		sc->sc_blkno = dbtofsb(bp->b_blkno);
		goto opcont;

	default:
		panic("utintr");
	}

opdone:
	/*
	 * Reset error count and remove
	 * from device queue
	 */
	um->um_tab.b_errcnt = 0;
	dp->b_actf = bp->b_forw;
	ubadone(um);
	iodone(bp);
	/*
	 * Circulate slave to end of controller queue
	 * to give other slaves a chance
	 */
	um->um_tab.b_actf = dp->b_forw;
	if (dp->b_actf) {
		dp->b_forw = NULL;
		if (um->um_tab.b_actf == NULL)
			um->um_tab.b_actf = dp;
		else
			um->um_tab.b_actl->b_forw = dp;
		um->um_tab.b_actl = dp;
	}
	if (um->um_tab.b_actf == 0)
		return;
opcont:
	utstart(um);
}

/*
 * Raw interface for a read
 */
utread(dev)
	dev_t dev;
{
	utphys(dev);
	if (u.u_error)
		return;
	physio(utstrategy, &rutbuf[UTUNIT(dev)], dev, B_READ, minphys);
}

/*
 * Raw interface for a write
 */
utwrite(dev)
{
	utphys(dev);
	if (u.u_error)
		return;
	physio(utstrategy, &rutbuf[UTUNIT(dev)], dev, B_WRITE, minphys);
}

/*
 * Check for valid device number dev and update our notion
 * of where we are on the tape
 */
utphys(dev)
	dev_t dev;
{
	register int tjunit = TJUNIT(dev);
	register struct tj_softc *sc;
	register struct uba_device *ui;
	register daddr_t a;

	if (tjunit >= NTJ || (ui=tjdinfo[tjunit]) == 0 || ui->ui_alive == 0) {
		u.u_error = ENXIO;
		return;
	}
	a = u.u_offset >> 9;
	sc = &tj_softc[tjunit];
	sc->sc_blkno = dbtofsb(a);
	sc->sc_nxrec = dbtofsb(a)+1;
}

/*ARGSUSED*/
utioctl(dev, cmd, addr, flag)
	dev_t dev;
	caddr_t addr;
{
	register struct tj_softc *sc = &tj_softc[TJUNIT(dev)];
	register struct buf *bp = &cutbuf[UTUNIT(dev)];
	register callcount;
	int fcount;
	struct mtop mtop;
	struct mtget mtget;
	/* we depend of the values and order of the MT codes here */
	static utops[] =
      {UT_WEOF,UT_SFORWF,UT_SREVF,UT_SFORW,UT_SREV,UT_REW,UT_REWOFFL,UT_SENSE};

	switch (cmd) {

	case MTIOCTOP:
		if (copyin((caddr_t)addr, (caddr_t)&mtop, sizeof(mtop))) {
			u.u_error = EFAULT;
			return;
		}
		switch(mtop.mt_op) {

		case MTWEOF:
			callcount = mtop.mt_count;
			fcount = 1;
			break;

		case MTFSF: case MTBSF:
		case MTFSR: case MTBSR:
			callcount = 1;
			fcount = mtop.mt_count;
			break;

		case MTREW: case MTOFFL: case MTNOP:
			callcount = 1;
			fcount = 1;
			break;

		default:
			u.u_error = ENXIO;
			return;
		}
		if (callcount <= 0 || fcount <= 0) {
			u.u_error = ENXIO;
			return;
		}
		while (--callcount >= 0) {
			utcommand(dev, utops[mtop.mt_op], fcount);
			if ((mtop.mt_op == MTFSR || mtop.mt_op == MTBSR) &&
			    bp->b_resid) {
				u.u_error = EIO;
				break;
			}
			if ((bp->b_flags&B_ERROR) || (sc->sc_dsreg&UTDS_BOT))
				break;
		}
		geterror(bp);
		return;

	case MTIOCGET:
		mtget.mt_dsreg = sc->sc_dsreg;
		mtget.mt_erreg = sc->sc_erreg;
		mtget.mt_resid = sc->sc_resid;
		mtget.mt_type = MT_ISUT;
		if (copyout((caddr_t)&mtget, addr, sizeof(mtget)))
			u.u_error = EFAULT;
		return;

	default:
		u.u_error = ENXIO;
	}
}

utreset(uban)
	int uban;
{
	register struct uba_ctlr *um;
	register ut11, tjunit;
	register struct uba_device *ui;
	register struct buf *dp;

	for (ut11 = 0; ut11 < NUT; ut11++) {
		if ((um = utminfo[ut11]) == 0 || um->um_alive == 0 ||
		   um->um_ubanum != uban)
			continue;
		printf(" ut%d", ut11);
		um->um_tab.b_active = 0;
		um->um_tab.b_actf = um->um_tab.b_actl = 0;
		if (um->um_ubinfo) {
			printf("<%d>", (um->um_ubinfo>>28)&0xf);
			ubadone(um);
		}
		((struct utdevice *)(um->um_addr))->utcs1 = UT_CLEAR|UT_GO;
		for (tjunit = 0; tjunit < NTJ; tjunit++) {
			if ((ui = tjdinfo[tjunit]) == 0 || ui->ui_mi != um ||
			    ui->ui_alive == 0)
				continue;
			dp = &tjutab[tjunit];
			dp->b_active = 0;
			dp->b_forw = 0;
			if (um->um_tab.b_actf == NULL)
				um->um_tab.b_actf = dp;
			else
				um->um_tab.b_actl->b_forw = dp;
			um->um_tab.b_actl = dp;
			if (tj_softc[tjunit].sc_openf > 0)
				tj_softc[tjunit].sc_openf = -1;
		}
		utstart(um);
	}
}

/*
 * Do a stand-alone core dump to tape --
 * from here down, routines are used only in dump context
 */
#define	DBSIZE	20

utdump()
{
	register struct uba_device *ui;
	register struct uba_regs *up;
	register struct utdevice *utaddr;
	int blk, num = maxfree;
	int start = 0;

#define	phys(a,b)		((b)((int)(a)&0x7fffffff))
	if (tjdinfo[0] == 0)
		return (ENXIO);
	ui = phys(tjdinfo[0], struct uba_device *);
	up = phys(ui->ui_hd, struct uba_hd *)->uh_physuba;
	ubainit();
	DELAY(1000000);
	utwait(utaddr);
	utaddr = (struct utdevice *)ui->ui_physaddr;
	/* do it at 1600 bpi so tape can be read on other machines */
	utaddr->uttc = UT_PE|PDP11FMT;	/* implicit slave 0 or-ed in */
	utaddr->utcs1 = UT_CLEAR|UT_GO;
	while (num > 0) {
		blk = num > DBSIZE ? DBSIZE : num;
		utdwrite(start, blk, utaddr, up);
		start += blk;
		num -= blk;
	}
	uteof(utaddr);
	uteof(utaddr);
	utwait(utaddr);
	if (utaddr->utds&UTDS_ERR)
		return(EIO);
	utaddr->utcs1 = UT_REW|UT_GO;
	return (0);
}

utdwrite(dbuf, num, utaddr, up)
	register dbuf, num;
	register struct utdevice *utaddr;
	struct uba_regs *up;
{
	register struct pte *io;
	register int npf;

	utwait(utaddr);
	io = up->uba_map;
	npf = num + 1;
	while (--npf != 0)
		*(int *)io++ = (dbuf++ | (1<<UBAMR_DPSHIFT) | UBAMR_MRV);
	*(int *)io = 0;
	utaddr->utwc = -((num*NBPG)<<1);
	utaddr->utba = 0;
	utaddr->utcs1 = UT_WCOM|UT_GO;
}

utwait(utaddr)
	struct utdevice *utaddr;
{
	register s;

	do
		s = utaddr->utds;
	while ((s&UTDS_DRY) == 0);
}

uteof(utaddr)
	struct utdevice *utaddr;
{

	utwait(utaddr);
	utaddr->utcs1 = UT_WEOF|UT_GO;
}
#endif
