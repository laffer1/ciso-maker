DESTDIR= 
PREFIX=	/usr/local/
CC=	gcc

INSTALL = install

#.if ${CC} == "clang"
#CFLAGS+=	-Wno-format -Wno-tautological-compare
#.endif

all : ciso-maker
ciso-maker : ciso.o
	${CC} -o ciso-maker ciso.o -lz

ciso.o : ciso.c
	${CC} -o ciso.o -c ciso.c

install :
	$(INSTALL) -m 755 ciso-maker $(DESTDIR)${PREFIX}/bin/ciso-maker

clean:
	rm -rf *.o ciso-maker
