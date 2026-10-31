CC	= gcc
# Board target: empty builds the ZZ9000 rows only (the original driver).
# A target build passes it on the make line, e.g.
#   make ZZ9K_TARGET=-DZZ9K_TARGET_Z3660
# Bench aids (never for a shipped kernel) go in a second variable so that a
# build hub which passes one token per variable can set both:
#   make ZZ9K_TARGET=-DZZ9K_TARGET_Z3660 ZZ9K_BENCH=-DZZ9K_BENCH_Z3660
# ZZ9K_OPTS carries one further single-token option, e.g. the Z3660 row's
# console geometry: make ... ZZ9K_OPTS=-DZZ9K_Z3660_CON_1280x800
ZZ9K_TARGET =
ZZ9K_BENCH =
ZZ9K_OPTS =
ZCOPTS	= -O -traditional -fno-builtin -Dinline=__inline__
ZCDEFS	= -D_KERNEL -DSVR40 -DSVR4 $(ZZ9K_TARGET) $(ZZ9K_BENCH) $(ZZ9K_OPTS)
ZCINCS	= -I../../.. -I../../inc
ZCFLAGS	= $(ZCOPTS) $(ZCDEFS) $(ZCINCS)

CFLAGS	= -O -traditional -fno-builtin -Dinline=__inline__ -D_KERNEL -DSVR40 -DSVR4 $(ZZ9K_TARGET) $(ZZ9K_BENCH) $(ZZ9K_OPTS) -I../../.. -I../../inc $(KDB)

OBJ	= zz9000.o
FONT	= ../../console/sunfont.o

exp:	$(OBJ) $(FONT)
	$(LD) -r -o exp $(OBJ) $(FONT)

install-tools:
	cd tools; $(MAKE) install-bin ROOT="$(ROOT)" CC="$(CC)"

installtools: install-tools

install-scripts:
	cd tools; $(MAKE) install-script ROOT="$(ROOT)" CC="$(CC)"

installscripts: install-scripts

install-rc:
	cd init; $(MAKE) install ROOT="$(ROOT)"

installrc: install-rc

install: install-tools install-rc

install-all: install-tools install-rc

installall: install-all

clean:
	-rm -f $(OBJ) exp
	cd tools; $(MAKE) clean CC="$(CC)"

zz9000.o : zz9000.c zz9000.h
	$(CC) $(ZCFLAGS) -c zz9000.c

../../console/sunfont.o : ../../console/sunfont.s
	cd ../../console; $(MAKE) sunfont.o
