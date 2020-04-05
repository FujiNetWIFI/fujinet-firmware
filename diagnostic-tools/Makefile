
##
## FujiNet tools Master Make file.
## Hack-o-matic, for sure, it will get better.
##

.PHONY: all mostlyclean clean install zip

.SUFFIXES:

all mostlyclean clean install zip:
	@$(MAKE) -C audio   --no-print-directory $@	
	@$(MAKE) -C thwap   --no-print-directory $@	

dist: all
	mkdir -p dist
	cp audio/audio.com dist/
	cp thwap/thwap.com dist/
	dir2atr 720 diags.atr dist/
