TESTS = test_fujibus test_fuji_cfg test_fuji_port test_fuji_store test_fuji_watchdog test_fuji_crc
test_fujibus_SRCS = $(SRC)/fujibus.c
test_fuji_cfg_SRCS = $(SRC)/fuji_cfg.c
test_fuji_port_SRCS = $(SRC)/fuji_port.c $(SRC)/fuji_cfg.c
test_fuji_port_CFLAGS = -Istub
test_fuji_store_SRCS = $(SRC)/fuji_store.c
# ASan, because a RAM-tier overrun lands in the next array and nothing else would see it.
test_fuji_store_CFLAGS = -Istub -DFUJI_STORE_BINARY_END=0x1000u \
    -fsanitize=address,undefined -fno-sanitize-recover=all
test_fuji_watchdog_SRCS = $(SRC)/fuji_watchdog.c
test_fuji_watchdog_CFLAGS = -Istub
test_fuji_crc_SRCS = $(SRC)/fuji_crc.c
C_ONLY_SRCS = $(SRC)/fuji_port.c $(SRC)/fuji_store.c $(SRC)/fuji_watchdog.c
OUT = ../build/host_test

include cart.mk

# emu/fujins.syms against what the modules export; fujimail.c needs some cart's mailbox map.
all:: | $(OUT)
	@echo "== fujins.syms"; python3 check_fujins.py $(OUT) ../../studio2/firmware/include
