# The mapper engine against MAME's sega8 handlers, the bus decode and 74HCT glue, the load
# sequence on a model of the board, the mailbox and the codec.
TESTS = test_smsmap test_busio test_fujimail test_load
MAP = $(SRC)/smsmap.c $(SRC)/smsmap_db.c $(COMMON)/src/fuji_crc.c
test_smsmap_SRCS = $(MAP)
test_fujimail_SRCS = $(COMMON)/src/fujimail.c $(MAP)
test_load_SRCS = $(SRC)/fuji_load.c $(MAP)

include ../../../common/host_test/cart.mk
