#	@(#)Makefile	8.1 (Berkeley) 05/31/93

PROG=	boggle
MAN6=	boggle.0
DPADD=	${LIBCOMPAT}
LDADD=	-lcompat
HIDEGAME=hidegame
CLEANFILES=comp bogdict

all: ${PROG} ${MAN6}

boggle: bogdict

bogdict: /usr/share/dict/words comp sfile
	sed -f ${.CURDIR}/sfile /usr/share/dict/words | uniq | ./comp >bogdict

beforeinstall:
	install -c -o ${BINOWN} -g ${BINGRP} -m 444 bogdict \
	    ${DESTDIR}/usr/share/games
comp:
	${CC} -O ${.CURDIR}/comp.c -o ${.TARGET}

.include <bsd.prog.mk>
