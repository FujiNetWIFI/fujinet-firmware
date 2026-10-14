SHELL := /bin/bash
# Host tests: no hardware, no pico-sdk. Binaries go to ../../build/host_test.
CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror -O1 -g -std=c11 -D_DEFAULT_SOURCE
INC = -I. -I../include -I../../../common/include
SRC = ../src
OUT = ../../build/host_test

CORE = $(SRC)/s100_regs.c $(SRC)/s100_fdc.c $(SRC)/s100_2sio.c $(SRC)/s100_link.c \
       ../../../common/src/fujibus.c
HDRS = $(wildcard ../include/*.h) check.h fake_esp.h

TESTS = test_regs test_2sio test_fdc test_link test_glue test_timing

# test_pio needs pioasm: on PATH, or the one build-cart.sh built.
PIOASM ?= $(shell command -v pioasm || ls ../../build/fw-*/pioasm-install/pioasm/pioasm 2>/dev/null | head -1)
ifneq ($(PIOASM),)
TESTS += test_pio
endif

all: $(addprefix $(OUT)/,$(TESTS))
	@for t in $(TESTS); do $(OUT)/$$t || exit 1; done
	@[ -n "$(PIOASM)" ] || echo "test_pio: SKIP (no pioasm; run ../../build-cart.sh once)"

$(OUT):
	mkdir -p $@

$(OUT)/s100_bus.pio.h: ../src/s100_bus.pio | $(OUT)
	$(PIOASM) -o c-sdk $< $@

$(OUT)/test_pio: test_pio.c $(OUT)/s100_bus.pio.h check.h ../include/s100_card.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -I$(OUT) -o $@ test_pio.c

$(OUT)/test_%: test_%.c $(CORE) $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ $< $(CORE)

clean:
	rm -rf $(OUT)

.PHONY: all clean
