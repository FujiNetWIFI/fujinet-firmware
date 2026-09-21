# Makefile for zmac

# Edit any defs which are wrong - these should be OK for Linux though.

CC = gcc

YACC = bison -y

MV = mv

ZIP = zip

CFLAGS = -O -Wall

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
#MANDIR = $(PREFIX)/man/man1
MANDIR = /usr/share/man/man1
# Changes path to manual for RH7.2 (Lance F. Squire)

all:	zmac

zmac:	zmac.c mio.c getoptn.c
	$(CC) $(CFLAGS) -o zmac zmac.c mio.c getoptn.c

zmac.c: zmac.y
	$(YACC) zmac.y
	$(MV) y.tab.c zmac.c

dev:	/tmp/zmac

/tmp/zmac:	zmac.c mio.c getoptn.c
	$(CC) $(CFLAGS) --ansi --pedantic --extra-warnings --all-warnings -o /tmp/zmac zmac.c mio.c getoptn.c
	chmod 777 /tmp/zmac

install: zmac
	install -s -m 511 zmac $(BINDIR)
	install -m 444 zmac.1 $(MANDIR)

zip:
	$(RM) zmac.zip
	man -l -7 zmac.1 | sed 's/.//g' >zmac.doc
	$(ZIP) zmac.zip zmac.y zmac.c mio.c mio.h getoptn.c getoptn.h zmac.1 \
	  zmac.doc Makefile README NEWS COPYRIGHT ChangeLog MAXAM RISCOS \
	  file_id.diz

clean:
	$(RM) *.o y.tab.c zmac.c *~ zmac
