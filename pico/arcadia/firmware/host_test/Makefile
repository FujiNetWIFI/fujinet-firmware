# test_arcmap sweeps the connector decode the serve loop and the MAME device must agree on;
# test_fujimail covers the hotspot mailbox and test_busedge core1's pin-change dedupe.
TESTS = test_arcmap test_fujimail test_busedge
test_arcmap_SRCS = $(SRC)/arcmap.c
test_fujimail_SRCS = $(COMMON)/src/fujimail.c $(SRC)/arcmap.c

include ../../../common/host_test/cart.mk
