CC = gcc
CFLAGS = -O -I..
ROOT =
BINDIR = $(ROOT)/usr/amiga/bin

all: zz9k_test colorls

zz9k_test: zz9k_test.o
	$(CC) -o zz9k_test zz9k_test.o

colorls: colorls.o
	$(CC) -o colorls colorls.o

install: install-bin

install-script:
	cp zz9k_console $(BINDIR)/zz9k_console
	cp zz9k_colorls_env $(BINDIR)/zz9k_colorls_env
	chmod 755 $(BINDIR)/zz9k_console $(BINDIR)/zz9k_colorls_env

install-bin: all
	cp zz9k_test $(BINDIR)/zz9k_test
	cp colorls $(BINDIR)/colorls
	cp zz9k_console $(BINDIR)/zz9k_console
	cp zz9k_colorls_env $(BINDIR)/zz9k_colorls_env
	chmod 755 $(BINDIR)/zz9k_test $(BINDIR)/colorls $(BINDIR)/zz9k_console $(BINDIR)/zz9k_colorls_env

clean:
	-rm -f zz9k_test.o zz9k_test colorls.o colorls

zz9k_test.o: ../zz9000.h

colorls.o: colorls.c
