SHELL := /bin/bash
# Host tests: no hardware, no pico-sdk. Binaries go to ../../build/host_test.
CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror -O1 -g -std=c11
INC = -I.. -I../include
SRC = ../src
OUT = ../../build/host_test

TESTS = test_fujibus test_smsmap test_busio test_fujimail test_load

all: $(addprefix $(OUT)/,$(TESTS))
	@for t in $(TESTS); do echo "== $$t"; $(OUT)/$$t || exit 1; done

$(OUT):
	mkdir -p $@

$(OUT)/test_fujibus: test_fujibus.c $(SRC)/fujibus.c ../include/fujibus.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_fujibus.c $(SRC)/fujibus.c

$(OUT)/test_smsmap: test_smsmap.c $(SRC)/smsmap.c $(SRC)/smsmap_db.c ../include/smsmap.h ../include/smsmap_db.h ../include/fuji_mailbox.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_smsmap.c $(SRC)/smsmap.c $(SRC)/smsmap_db.c

$(OUT)/test_fujimail: test_fujimail.c $(SRC)/fujimail.c $(SRC)/smsmap.c $(SRC)/smsmap_db.c ../include/fujimail.h ../include/fuji_mailbox.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_fujimail.c $(SRC)/fujimail.c $(SRC)/smsmap.c $(SRC)/smsmap_db.c

$(OUT)/test_busio: test_busio.c ../include/sms_cart.h ../include/smsmap.h ../include/fuji_mailbox.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_busio.c

$(OUT)/test_load: test_load.c $(SRC)/fuji_load.c $(SRC)/smsmap.c $(SRC)/smsmap_db.c ../include/fuji_load.h ../include/sms_cart.h ../include/smsmap.h | $(OUT)
	$(CC) $(CFLAGS) $(INC) -o $@ test_load.c $(SRC)/fuji_load.c $(SRC)/smsmap.c $(SRC)/smsmap_db.c

clean:
	rm -rf $(OUT)

.PHONY: all clean
