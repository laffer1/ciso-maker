DESTDIR= 
PREFIX?=	/usr/local
MANPREFIX?=	${PREFIX}
VERSION?=	1.1.0
CC?=	cc
CPPFLAGS?=	-DCISO_MAKER_VERSION=\"${VERSION}\"
CFLAGS?= -Wall -pedantic -std=c99 -O2
LDFLAGS?=
INSTALL = install

MAN=	ciso-maker.1

all : clean ciso-maker
ciso-maker : ciso.o
	${CC} ${CFLAGS} ${LDFLAGS} -o ciso-maker ciso.o -lz

ciso.o : ciso.c
	${CC} ${CPPFLAGS} ${CFLAGS} -o ciso.o -c ciso.c

install :
	$(INSTALL) -m 755 ciso-maker $(DESTDIR)${PREFIX}/bin/ciso-maker
	$(INSTALL) -m 644 ${MAN} ${DESTDIR}${MANPREFIX}/man/man1

clean:
	rm -rf *.o ciso-maker
