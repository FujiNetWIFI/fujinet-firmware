SHELL := /bin/bash
# Host tests: no hardware, no pico-sdk. Binaries go to ../../build/host_test.
CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror -O1 -g -std=c11
INC = -I.. -I../include
SRC = ../src
OUT = ../../build/host_test
# the boot block: build-cart.sh's fresh one if there, else the baked one
GEN = $(firstword $(wildcard ../../build/gen/a78bootblk.h) ../baked/a78bootblk.h)

TESTS = test_fujibus test_a78map test_busio test_glue test_pio test_pokey \
        test_load test_hsc test_fujimail test_bootblock

all: $(addprefix $(OUT)/,$(TESTS))
	@for t in $(TESTS); do echo "== $$t"; $(OUT)/$$t || exit 1; done
	@echo "== signature"; python3 ../../tools/mkbootblk.py --verify $(GEN) $(OUT)

$(OUT):
	mkdir -p $@

MAP = $(SRC)/a78map.c $(SRC)/a78map_db.c
MAPH = ../include/a78map.h ../include/a78map_db.h ../include/a78_cart.h ../include/fuji_mailbox.h

$(OUT)/test_fujibus: test_fujibus.c $(SRC)/fujibus.c ../include/fujibus.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_fujibus.c $(SRC)/fujibus.c

$(OUT)/test_a78map: test_a78map.c $(MAP) $(SRC)/fuji_load.c $(MAPH) ../include/fuji_load.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_a78map.c $(MAP) $(SRC)/fuji_load.c

$(OUT)/test_busio: test_busio.c $(MAPH) | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_busio.c

$(OUT)/test_glue: test_glue.c $(MAP) $(MAPH) | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_glue.c $(MAP)

$(OUT)/test_pio: test_pio.c ../include/a78_pio.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_pio.c

$(OUT)/test_pokey: test_pokey.c $(SRC)/pokey.c ../include/pokey.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_pokey.c $(SRC)/pokey.c

$(OUT)/test_load: test_load.c $(SRC)/fuji_load.c $(MAP) $(MAPH) ../include/fuji_load.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_load.c $(SRC)/fuji_load.c $(MAP)

$(OUT)/test_hsc: test_hsc.c $(SRC)/hsc.c ../include/hsc.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_hsc.c $(SRC)/hsc.c

$(OUT)/test_fujimail: test_fujimail.c $(SRC)/fujimail.c $(MAP) ../include/fujimail.h ../include/fuji_mailbox.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_fujimail.c $(SRC)/fujimail.c $(MAP)

$(OUT)/test_bootblock: test_bootblock.c $(GEN) ../include/fuji_mailbox.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -I$(dir $(GEN)) -o $@ test_bootblock.c

clean:
	rm -rf $(OUT)

.PHONY: all clean
