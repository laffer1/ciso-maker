DESTDIR= 
PREFIX?=	/usr/local/
CC?=	gcc
CFLAGS?= -Wall
LDFLAGS?=
INSTALL = install

#.if ${CC} == "clang"
#CFLAGS+=	-Wno-format -Wno-tautological-compare
#.endif

all : clean ciso-maker
ciso-maker : ciso.o
	${CC} ${CFLAGS} ${LDFLAGS} -o ciso-maker ciso.o -lz

ciso.o : ciso.c
	${CC} ${CFLAGS} -o ciso.o -c ciso.c

install :
	$(INSTALL) -m 755 ciso-maker $(DESTDIR)${PREFIX}/bin/ciso-maker

clean:
	rm -rf *.o ciso-maker
