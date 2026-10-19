# The mapper engine against MAME's a5200 handlers, the bus observer, the '541 glue, staging
# and hand-over, the mailbox and the codec. A52_CORPUS=/tmp/a5200 also plans every image there.
TESTS = test_fujibus test_a52map test_busio test_glue test_load test_fujimail
MAP = $(SRC)/a52map.c $(SRC)/a52map_db.c
test_fujibus_SRCS = $(SRC)/fujibus.c
test_a52map_SRCS = $(MAP)
test_busio_SRCS = $(SRC)/fuji_load.c $(MAP)
test_load_SRCS = $(SRC)/fuji_load.c $(MAP)
test_fujimail_SRCS = $(SRC)/fujimail.c $(MAP)

include ../../../common/host_test/cart.mk
