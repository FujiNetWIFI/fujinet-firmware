SHELL := /bin/bash
# Host tests: no hardware, no pico-sdk. Binaries go to ../../build/host_test.
# A52_CORPUS=/tmp/a5200 also plans every image there.
CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror -O1 -g -std=c11
INC = -I.. -I../include
SRC = ../src
OUT = ../../build/host_test

TESTS = test_fujibus test_a52map test_busio test_glue test_load test_fujimail

all: $(addprefix $(OUT)/,$(TESTS))
	@for t in $(TESTS); do echo "== $$t"; $(OUT)/$$t || exit 1; done

$(OUT):
	mkdir -p $@

MAP = $(SRC)/a52map.c $(SRC)/a52map_db.c
MAPH = ../include/a52map.h ../include/a52map_db.h ../include/a52_cart.h ../include/fuji_mailbox.h

$(OUT)/test_fujibus: test_fujibus.c $(SRC)/fujibus.c ../include/fujibus.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_fujibus.c $(SRC)/fujibus.c

$(OUT)/test_a52map: test_a52map.c $(MAP) $(MAPH) | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_a52map.c $(MAP)

$(OUT)/test_busio: test_busio.c $(SRC)/fuji_load.c $(MAP) $(MAPH) ../include/fuji_load.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_busio.c $(SRC)/fuji_load.c $(MAP)

$(OUT)/test_glue: test_glue.c $(MAPH) | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_glue.c

$(OUT)/test_load: test_load.c $(SRC)/fuji_load.c $(MAP) $(MAPH) ../include/fuji_load.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_load.c $(SRC)/fuji_load.c $(MAP)

$(OUT)/test_fujimail: test_fujimail.c $(SRC)/fujimail.c $(MAP) ../include/fujimail.h ../include/fuji_mailbox.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_fujimail.c $(SRC)/fujimail.c $(MAP)

clean:
	rm -rf $(OUT)

.PHONY: all clean
