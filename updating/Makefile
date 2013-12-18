# $FreeBSD: stable/10/usr.sbin/pkg_install/updating/Makefile 222035 2011-05-17 19:11:47Z flz $

PROG=	pkg_updating
SRCS=	main.c

CFLAGS+= -I${.CURDIR}/../lib

WARNS=	6
WFORMAT?= 1

DPADD=	${LIBINSTALL} ${LIBFETCH} ${LIBMD}
LDADD=	${LIBINSTALL} -lfetch -lmd

.include <bsd.prog.mk>
