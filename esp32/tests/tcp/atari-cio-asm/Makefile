##
# Makefile for N: device
#

ATASM=atasm


.PHONY: all

.SUFFIXES:

all: autorun.sys

clean:
	$(RM) autorun.sys

autorun.sys: 
	$(ATASM) -vsu -oautorun.sys src/autorun.asm

dist: autorun.sys
	cp autorun.sys dist/
	dir2atr -b Dos25 720 ../data/tcp.atr dist/
