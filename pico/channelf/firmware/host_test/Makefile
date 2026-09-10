SHELL := /bin/bash
# Host tests: no hardware, no pico-sdk. CI runs this as a single `make` step.
CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror -O1 -g
INC = -I.. -I../include
SRC = ../src
BUILD = ../../build

# test_busio compiles the observer with cart I/O ports claimed, which is the
# only way to exercise the ROMC 03 -> 1A/1B latch.
BUSIO_FLAGS = -DCHF_IO_LO=0x20 -DCHF_IO_HI=0x27

TESTS = test_busio test_romc

all: $(TESTS)
	@echo "== test_busio"; ./test_busio
	@echo "== test_romc"
	@if [ ! -f $(BUILD)/trace/hello.trace ] || [ ! -f $(BUILD)/trace/romctest.trace ]; then \
	    echo "   SKIPPED: no golden traces. Generate them with tools/mktrace.sh <mame-tree>"; \
	elif ! sha256sum -c --status \
	         <(sed "s|$$| *$(BUILD)/hello.bin|"    $(BUILD)/trace/hello.sha) \
	         <(sed "s|$$| *$(BUILD)/romctest.bin|" $(BUILD)/trace/romctest.sha) 2>/dev/null; then \
	    echo "   STALE: the clients have been rebuilt since these traces were captured."; \
	    echo "   Re-run tools/mktrace.sh <mame-tree>; a stale trace fails as a register"; \
	    echo "   divergence that looks exactly like an observer bug."; \
	    exit 1; \
	else \
	    ./test_romc $(BUILD)/trace/hello.trace   $(BUILD)/hello.bin \
	                $(BUILD)/trace/romctest.trace $(BUILD)/romctest.bin; \
	fi

test_romc: test_romc.c ../include/channelf_cart.h
	$(CC) $(CFLAGS) $(INC) -o $@ $<

test_busio: test_busio.c ../include/channelf_cart.h
	$(CC) $(CFLAGS) $(INC) $(BUSIO_FLAGS) -o $@ $<

clean:
	rm -f $(TESTS)

.PHONY: all clean
