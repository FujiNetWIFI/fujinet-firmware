# The mapper engine against MAME's sega8 handlers, the bus decode and 74HCT glue, the load
# sequence on a model of the board, the mailbox and the codec.
TESTS = test_fujibus test_smsmap test_busio test_fujimail test_load
MAP = $(SRC)/smsmap.c $(SRC)/smsmap_db.c
test_fujibus_SRCS = $(SRC)/fujibus.c
test_smsmap_SRCS = $(MAP)
test_fujimail_SRCS = $(SRC)/fujimail.c $(MAP)
test_load_SRCS = $(SRC)/fuji_load.c $(MAP)

include ../../../common/host_test/cart.mk
