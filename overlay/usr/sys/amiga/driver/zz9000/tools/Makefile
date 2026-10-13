CC = gcc
CFLAGS = -O -I..
ROOT =
BINDIR = $(ROOT)/usr/amiga/bin

all: zz9k_test zz9k_reg zz9k_cat zz9k_rw zz9k_maptest zz9k_mode zz9k_monsw colorls

zz9k_test: zz9k_test.o
	$(CC) -o zz9k_test zz9k_test.o

colorls: colorls.o
	$(CC) -o colorls colorls.o

zz9k_reg: zz9k_reg.o
	$(CC) -o zz9k_reg zz9k_reg.o

zz9k_cat: zz9k_cat.o
	$(CC) -o zz9k_cat zz9k_cat.o

zz9k_rw: zz9k_rw.o
	$(CC) -o zz9k_rw zz9k_rw.o

zz9k_maptest: zz9k_maptest.o
	$(CC) -o zz9k_maptest zz9k_maptest.o

zz9k_mode: zz9k_mode.o
	$(CC) -o zz9k_mode zz9k_mode.o

# root-only: drives the monitor-switch line(s) through /dev/mem (startx-rtg's fallback)
zz9k_monsw: zz9k_monsw.o
	$(CC) -o zz9k_monsw zz9k_monsw.o

# root-only /dev/mem diagnostic for the Z3660 window; not part of `all`
zz9k_memprobe: zz9k_memprobe.c
	$(CC) $(CFLAGS) -o zz9k_memprobe zz9k_memprobe.c

install: install-bin

install-script:
	cp zz9k_console $(BINDIR)/zz9k_console
	cp zz9k_demo $(BINDIR)/zz9k_demo
	cp zz9k_colorls_env $(BINDIR)/zz9k_colorls_env
	chmod 755 $(BINDIR)/zz9k_console $(BINDIR)/zz9k_demo $(BINDIR)/zz9k_colorls_env

install-bin: all
	cp zz9k_test $(BINDIR)/zz9k_test
	cp zz9k_reg $(BINDIR)/zz9k_reg
	cp zz9k_cat $(BINDIR)/zz9k_cat
	cp zz9k_rw $(BINDIR)/zz9k_rw
	cp zz9k_maptest $(BINDIR)/zz9k_maptest
	cp zz9k_mode $(BINDIR)/zz9k_mode
	cp zz9k_monsw $(BINDIR)/zz9k_monsw
	cp colorls $(BINDIR)/colorls
	cp zz9k_console $(BINDIR)/zz9k_console
	cp zz9k_demo $(BINDIR)/zz9k_demo
	cp zz9k_colorls_env $(BINDIR)/zz9k_colorls_env
	chmod 755 $(BINDIR)/zz9k_test $(BINDIR)/zz9k_reg $(BINDIR)/zz9k_cat $(BINDIR)/zz9k_rw $(BINDIR)/zz9k_maptest $(BINDIR)/zz9k_mode $(BINDIR)/zz9k_monsw $(BINDIR)/colorls $(BINDIR)/zz9k_console $(BINDIR)/zz9k_demo $(BINDIR)/zz9k_colorls_env

clean:
	-rm -f zz9k_test.o zz9k_test zz9k_reg.o zz9k_reg zz9k_cat.o zz9k_cat zz9k_rw.o zz9k_rw zz9k_maptest.o zz9k_maptest zz9k_mode.o zz9k_mode zz9k_monsw.o zz9k_monsw zz9k_memprobe colorls.o colorls

zz9k_test.o: ../zz9000.h

zz9k_reg.o: ../zz9000.h

zz9k_cat.o: ../zz9000.h

zz9k_rw.o:

zz9k_maptest.o: ../zz9000.h

zz9k_mode.o: ../zz9000.h

colorls.o: colorls.c
