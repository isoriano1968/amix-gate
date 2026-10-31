CC=gcc
CFLAGS=-g -I..
LDFLAGS=
LIBS=

all: hya hydra_test

hya: hya.o
	$(CC) $(LDFLAGS) hya.o -o hya $(LIBS)

hydra_test: hydra_test.o
	$(CC) $(LDFLAGS) hydra_test.o -o hydra_test $(LIBS)

hya.o: hya.c hydrauser.h ../ne2000.h
	$(CC) $(CFLAGS) -c hya.c -o hya.o

hydra_test.o: hydra_test.c hydrauser.h ../ne2000.h
	$(CC) $(CFLAGS) -c hydra_test.c -o hydra_test.o

clean:
	rm -f *.o hya hydra_test core

clobber: clean

install: hya hydra_test
	cp hya $(ROOT)/usr/amiga/bin/hya
	cp hydra_test $(ROOT)/usr/amiga/bin/hydra_test
	chmod 755 $(ROOT)/usr/amiga/bin/hya $(ROOT)/usr/amiga/bin/hydra_test
