
##
## FujiNet tools Master Make file.
## Hack-o-matic, for sure, it will get better.
##

.PHONY: all mostlyclean clean install zip

.SUFFIXES:

all mostlyclean clean install zip:
	@$(MAKE) -C nopen    --no-print-directory $@	
	@$(MAKE) -C nclose   --no-print-directory $@	
	@$(MAKE) -C nwrite   --no-print-directory $@	
	@$(MAKE) -C nstatus  --no-print-directory $@	
	@$(MAKE) -C nread    --no-print-directory $@	
	@$(MAKE) -C naccept  --no-print-directory $@
	@$(MAKE) -C nhdrtogl --no-print-directory $@
	@$(MAKE) -C nhdrcoll --no-print-directory $@
	@$(MAKE) -C nwrifile --no-print-directory $@
	@$(MAKE) -C ncacert  --no-print-directory $@
	@$(MAKE) -C nstest   --no-print-directory $@

dist: all
	mkdir -p dist
	cp nopen/nopen.com dist/
	cp nclose/nclose.com dist/
	cp nwrite/nwrite.com dist/
	cp nstatus/nstatus.com dist/
	cp nread/nread.com dist/
	cp naccept/naccept.com dist/
	cp nhdrtogl/nhdrtogl.com dist/
	cp nhdrcoll/nhdrcoll.com dist/
	cp nwrifile/nwrifile.com dist/
	cp ncacert/ncacert.com dist/
	cp nstest/nstest.com dist/
	cp test-scripts/THTTP.EXC dist/
	cp ../../../tls-root-certs/atascii/DIGICERT.CER dist/
	cp DOS.SYS dist/
	dir2atr -b DosXL230 720 n-tests.atr dist/
