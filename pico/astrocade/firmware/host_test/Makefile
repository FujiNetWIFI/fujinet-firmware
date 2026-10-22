# test_bankserve checks the banked serve model against MAME's handlers; test_fujistore runs
# the push store on pico/common's host_test/stub/, which models NOR programming.
TESTS = test_astromap test_bankserve test_fujimail test_fujistore
test_astromap_SRCS = $(SRC)/astromap.c
test_bankserve_SRCS = $(SRC)/astromap.c
test_fujimail_SRCS = $(COMMON)/src/fujimail.c $(SRC)/astromap.c
test_fujistore_SRCS = $(SRC)/fuji_store.c
test_fujistore_CFLAGS = -I$(COMMON)/host_test/stub -DFUJI_STORE_BINARY_END=0x1000u
C_ONLY_SRCS = $(SRC)/fuji_store.c

include ../../../common/host_test/cart.mk
