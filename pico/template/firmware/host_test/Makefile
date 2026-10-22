# The image mapper, and pico/common's mailbox service against this cart's fuji_mailbox.h.
TESTS = test_tmplmap test_fujimail
test_tmplmap_SRCS = $(SRC)/tmplmap.c
test_fujimail_SRCS = $(COMMON)/src/fujimail.c $(SRC)/tmplmap.c

include ../../../common/host_test/cart.mk
