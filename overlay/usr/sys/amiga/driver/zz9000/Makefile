CC	= gcc
ZCOPTS	= -O -traditional -fno-builtin -Dinline=__inline__
ZCDEFS	= -D_KERNEL -DSVR40 -DSVR4
ZCINCS	= -I../../.. -I../../inc
ZCFLAGS	= $(ZCOPTS) $(ZCDEFS) $(ZCINCS)

CFLAGS	= -O -traditional -fno-builtin -Dinline=__inline__ -D_KERNEL -DSVR40 -DSVR4 -I../../.. -I../../inc $(KDB)

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
