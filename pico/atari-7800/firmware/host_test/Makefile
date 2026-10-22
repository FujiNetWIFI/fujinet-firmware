# The mapper engine against MAME's a7800 handlers, the bus decode and glue, the PIO slot
# table, POKEY against MAME's polynomials, loading, the HSC, the mailbox, the codec and the
# signed boot block.
# the boot block: build-cart.sh's fresh one if there, else the baked one
GEN = $(firstword $(wildcard ../../build/gen/a78bootblk.h) ../baked/a78bootblk.h)

TESTS = test_a78map test_busio test_glue test_pio test_pokey \
        test_load test_hsc test_fujimail test_bootblock
MAP = $(SRC)/a78map.c $(SRC)/a78map_db.c $(COMMON)/src/fuji_crc.c
test_a78map_SRCS = $(MAP) $(SRC)/fuji_load.c
test_glue_SRCS = $(MAP)
test_pokey_SRCS = $(SRC)/pokey.c
test_load_SRCS = $(SRC)/fuji_load.c $(MAP)
test_hsc_SRCS = $(SRC)/hsc.c
test_fujimail_SRCS = $(COMMON)/src/fujimail.c $(MAP)
test_bootblock_DEPS = $(GEN)
test_bootblock_CFLAGS = -I$(dir $(GEN))

include ../../../common/host_test/cart.mk

all::
	@echo "== signature"; python3 ../../tools/mkbootblk.py --verify $(GEN) $(OUT)
