# $FreeBSD: stable/10/usr.sbin/pkg_install/Makefile.inc 222035 2011-05-17 19:11:47Z flz $

.include <bsd.own.mk>

LIBINSTALL=    ${.OBJDIR}/../lib/libinstall.a

CFLAGS+=		-I/usr/local/include
DPADD+=                ${LIBUTIL}
LDADD+=                -lutil -L/usr/local/lib -lpkg

.if ${MK_OPENSSL} != "no" && \
    defined(LDADD) && ${LDADD:M-lfetch} != ""
DPADD+=                ${LIBSSL} ${LIBCRYPTO}
LDADD+=                -lssl -lcrypto
.endif

