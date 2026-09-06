# Desktop regression tests: no pico-sdk, no console, no emulator.
#
# The four pieces most likely to be wrong in a way that is painful to diagnose
# on real hardware are all hardware-free by design, so they run here:
#
#   fujibus   the SLIP + FujiBus wire codec
#   colmap    image planning and every ColecoVision mapper, checked against a
#             verbatim transcription of MAME's own cartridge handlers
#   fujimail  the hotspot mailbox: register decode through read pairs, the
#             SEQ/ACKSEQ interlock, the DBC push receiver, and proof that a
#             stray read cannot mutate anything
#   busedge   the two-tier bus loop's decisions, including the RAM-mirror
#             aliasing that makes the second tier necessary
#
# make        build and run all four
# make clean

CC      ?= cc
CFLAGS  ?= -Wall -Wextra -Werror -O1 -g
INC      = -I.. -I../include
SRC      = ../src

TESTS = test_fujibus test_colmap test_fujimail test_busedge

.PHONY: all clean
all: $(TESTS)
	@for t in $(TESTS); do echo "--- $$t"; ./$$t || exit 1; done

test_fujibus: test_fujibus.c $(SRC)/fujibus.c
	$(CC) $(CFLAGS) $(INC) -o $@ $^

test_colmap: test_colmap.c $(SRC)/colmap.c
	$(CC) $(CFLAGS) $(INC) -o $@ $^

test_fujimail: test_fujimail.c $(SRC)/fujimail.c $(SRC)/colmap.c
	$(CC) $(CFLAGS) $(INC) -o $@ $^

test_busedge: test_busedge.c $(SRC)/colmap.c
	$(CC) $(CFLAGS) $(INC) -o $@ $^

clean:
	rm -f $(TESTS)
