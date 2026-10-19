# The codec, the mapper engine against MAME handler transcriptions, the bus decode and RMW
# signature, the PIO bank-table model, and the mailbox on the 4K arena.
TESTS = test_nesmap test_busio test_pio test_fujimail
test_nesmap_SRCS = $(SRC)/nesmap.c
test_fujimail_SRCS = $(COMMON)/src/fujimail.c $(SRC)/nesmap.c

include ../../../common/host_test/cart.mk
