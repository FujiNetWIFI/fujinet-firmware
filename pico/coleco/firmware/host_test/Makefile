TESTS = test_colmap test_fujimail test_busedge test_swap
test_colmap_SRCS = $(SRC)/colmap.c $(COMMON)/src/fuji_cfg.c
test_fujimail_SRCS = $(COMMON)/src/fujimail.c $(SRC)/colmap.c
test_busedge_SRCS = $(SRC)/colmap.c
test_swap_SRCS = $(SRC)/fuji_cart.c $(SRC)/fujinet.c $(SRC)/colmap.c $(COMMON)/src/fujimail.c

include ../../../common/host_test/cart.mk
