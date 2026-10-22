# The FujiBus codec self-test, the .cfg sibling's mapper= line, and what fuji_port hands
# fujimail.
TESTS = test_fujibus test_fuji_cfg test_fuji_port
test_fujibus_SRCS = $(SRC)/fujibus.c
test_fuji_cfg_SRCS = $(SRC)/fuji_cfg.c
test_fuji_port_SRCS = $(SRC)/fuji_port.c $(SRC)/fuji_cfg.c
test_fuji_port_CFLAGS = -Istub
C_ONLY_SRCS = $(SRC)/fuji_port.c
OUT = ../build/host_test

include cart.mk
