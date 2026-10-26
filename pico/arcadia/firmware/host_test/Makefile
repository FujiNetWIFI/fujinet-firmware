# test_arcmap sweeps the connector decode the serve loop and the MAME device must agree on;
# test_arcapp covers protocol 2's window, bank select and cart RAM, test_fujicart the two
# image slots and the swap, test_tick the painted 60 Hz count; test_fujimail covers the
# hotspot mailbox and test_busedge core1's pin-change dedupe.
TESTS = test_arcmap test_arcapp test_fujicart test_tick test_fujimail test_busedge
test_arcmap_SRCS = $(SRC)/arcmap.c
test_arcapp_SRCS = $(SRC)/arcmap.c
test_fujicart_SRCS = $(SRC)/fuji_cart.c $(SRC)/arcmap.c
test_fujimail_SRCS = $(COMMON)/src/fujimail.c $(SRC)/arcmap.c

include ../../../common/host_test/cart.mk
