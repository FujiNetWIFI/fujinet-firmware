##
## NOS Makefile
##

ifeq ($(OUTFILE),)
OUTFILE := nos.xfd
endif

.PHONY: all clean

all: xfdtoatr nos.xfd

xfdtoatr:
	gcc -otools/xfdtoatr tools/xfdtoatr.c

nos.xfd: xfdtoatr
	mads src/nos.s -l:nos.lst -o:$(OUTFILE)
	tools/xfdtoatr nos.xfd nos.atr

clean:
	rm -rf nos* dist/nos* tools/xfdtoatr.exe tools/xfdtoatr

