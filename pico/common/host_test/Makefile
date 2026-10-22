# The FujiBus codec self-test, the .cfg sibling's mapper= line, what fuji_port hands
# fujimail, and the push store on stub/'s model of NOR flash.
TESTS = test_fujibus test_fuji_cfg test_fuji_port test_fuji_store
test_fujibus_SRCS = $(SRC)/fujibus.c
test_fuji_cfg_SRCS = $(SRC)/fuji_cfg.c
test_fuji_port_SRCS = $(SRC)/fuji_port.c $(SRC)/fuji_cfg.c
test_fuji_port_CFLAGS = -Istub
test_fuji_store_SRCS = $(SRC)/fuji_store.c
# ASan, because a RAM-tier overrun lands in the next array and nothing else would see it.
test_fuji_store_CFLAGS = -Istub -DFUJI_STORE_BINARY_END=0x1000u \
    -fsanitize=address,undefined -fno-sanitize-recover=all
C_ONLY_SRCS = $(SRC)/fuji_port.c $(SRC)/fuji_store.c
OUT = ../build/host_test

include cart.mk
