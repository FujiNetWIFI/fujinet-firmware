##
## N: Handler Makefile
##

ifeq ($(RUNADDRESS),)
RUNADDRESS := 8960
endif
ifeq ($(OUTFILE),)
OUTFILE := ndev.com
endif

.PHONY: all clean

all: ndev.com

ndev.com:
	mads src/ndev.s -b:$(RUNADDRESS) -l:ndev.lst -o:$(OUTFILE)

dist: ndev.com
	cp ndev.com dist/
	dir2atr -S -b DosXL230 n-handler.atr dist

clean:
	rm -rf ndev* dist/ndev* n-handler.atr relwork-dist/ndev*

