#	@(#)Makefile	8.1 (Berkeley) 08/14/93

DIR=	usd/16.awk
SRCS=	awk
MACROS=	-msU
REFER= refer -e -p /usr/old/dict/papers/Ind

paper.ps: ${SRCS}
	${REFER} ${SRCS} | ${TBL} | ${ROFF} > ${.TARGET}

.include <bsd.doc.mk>
