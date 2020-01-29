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
