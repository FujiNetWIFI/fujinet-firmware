# S2_CORPUS=/tmp/studio2 also plans every image there; test_cycles replays the MAME traces
# in ../../build/traces if tools/mktrace.sh recorded any.
EXTRA_CFLAGS = -D_DEFAULT_SOURCE

TESTS = test_s2map test_busio test_glue test_text test_load test_fujimail test_cycles
MAP = $(SRC)/s2map.c
TEXT = $(SRC)/s2_text.c $(SRC)/s2_font.c
test_s2map_SRCS = $(MAP)
test_busio_SRCS = $(SRC)/fuji_load.c $(MAP)
test_text_SRCS = $(TEXT)
test_load_SRCS = $(SRC)/fuji_load.c $(MAP)
test_fujimail_SRCS = $(COMMON)/src/fujimail.c $(MAP)
test_cycles_SRCS = $(SRC)/fuji_load.c $(COMMON)/src/fujimail.c $(TEXT) $(MAP)

include ../../../common/host_test/cart.mk
