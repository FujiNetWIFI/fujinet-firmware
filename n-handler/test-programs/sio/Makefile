
##
## FujiNet tools Master Make file.
## Hack-o-matic, for sure, it will get better.
##

.PHONY: all mostlyclean clean install zip

.SUFFIXES:

all mostlyclean clean install zip:
	@$(MAKE) -C nopen   --no-print-directory $@	
	@$(MAKE) -C nclose  --no-print-directory $@	

dist: all
	mkdir -p dist
	cp nopen/nopen.com dist/
	cp nclose/nclose.com dist/
	dir2atr 720 n-tests.atr dist/
