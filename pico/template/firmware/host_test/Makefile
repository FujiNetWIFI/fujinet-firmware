# The image mapper, the cart's port hooks, and pico/common's mailbox service against this
# cart's fuji_mailbox.h.
TESTS = test_tmplmap test_port test_fujimail
test_tmplmap_SRCS = $(SRC)/tmplmap.c
test_port_SRCS = $(SRC)/fujinet.c $(SRC)/tmplmap.c
test_fujimail_SRCS = $(COMMON)/src/fujimail.c $(SRC)/tmplmap.c

C_ONLY_SRCS = $(SRC)/fujinet.c

include ../../../common/host_test/cart.mk
