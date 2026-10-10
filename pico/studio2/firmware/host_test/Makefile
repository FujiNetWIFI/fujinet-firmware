SHELL := /bin/bash
# Host tests: no hardware, no pico-sdk. Binaries go to ../../build/host_test.
# S2_CORPUS=/tmp/studio2 also plans every image there; test_cycles replays
# the MAME traces in ../../build/traces if tools/mktrace.sh recorded any.
CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror -O1 -g -std=c11 -D_DEFAULT_SOURCE
INC = -I.. -I../include
SRC = ../src
OUT = ../../build/host_test

TESTS = test_fujibus test_s2map test_busio test_glue test_text test_load test_fujimail test_cycles

all: $(addprefix $(OUT)/,$(TESTS))
	@for t in $(TESTS); do echo "== $$t"; $(OUT)/$$t || exit 1; done

$(OUT):
	mkdir -p $@

MAP = $(SRC)/s2map.c
MAPH = ../include/s2map.h ../include/s2_cart.h ../include/fuji_mailbox.h

$(OUT)/test_fujibus: test_fujibus.c $(SRC)/fujibus.c ../include/fujibus.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_fujibus.c $(SRC)/fujibus.c

$(OUT)/test_s2map: test_s2map.c $(MAP) $(MAPH) | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_s2map.c $(MAP)

$(OUT)/test_busio: test_busio.c $(SRC)/fuji_load.c $(MAP) $(MAPH) ../include/fuji_load.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_busio.c $(SRC)/fuji_load.c $(MAP)

$(OUT)/test_glue: test_glue.c $(MAPH) | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_glue.c

$(OUT)/test_text: test_text.c $(SRC)/s2_text.c $(SRC)/s2_font.c ../include/s2_text.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_text.c $(SRC)/s2_text.c $(SRC)/s2_font.c

$(OUT)/test_load: test_load.c $(SRC)/fuji_load.c $(MAP) $(MAPH) ../include/fuji_load.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_load.c $(SRC)/fuji_load.c $(MAP)

$(OUT)/test_fujimail: test_fujimail.c $(SRC)/fujimail.c $(MAP) ../include/fujimail.h ../include/fuji_mailbox.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_fujimail.c $(SRC)/fujimail.c $(MAP)

$(OUT)/test_cycles: test_cycles.c $(SRC)/fuji_load.c $(SRC)/fujimail.c $(SRC)/s2_text.c $(SRC)/s2_font.c $(MAP) $(MAPH) ../include/fuji_load.h ../include/s2_text.h ../include/fujimail.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_cycles.c $(SRC)/fuji_load.c $(SRC)/fujimail.c $(SRC)/s2_text.c $(SRC)/s2_font.c $(MAP)

clean:
	rm -rf $(OUT)

.PHONY: all clean
