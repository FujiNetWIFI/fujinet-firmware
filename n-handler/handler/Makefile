##
## N: Handler Makefile
##

.PHONY: all clean

all: ndev.com

ndev.com:
	atasm -ondev.com -lndev.lab -gndev.lst src/ndev.s

dist: ndev.com
	cp ndev.com dist/
	dir2atr -S -b DosXL230 n-handler.atr dist

clean:
	rm -rf ndev.* dist/ndev.com n-handler.atr
