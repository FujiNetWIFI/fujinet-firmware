# The F8 has no address bus, so these cover a bus protocol: test_romc replays golden MAME
# traces through the ROMC observer, and skips without them.
BUILD = ../../build

TESTS = test_busio test_chfmap test_fujicart test_tick
test_chfmap_SRCS = $(SRC)/chfmap.c
test_fujicart_SRCS = $(SRC)/fuji_cart.c $(SRC)/chfmap.c
# with cart I/O ports claimed, the only way to exercise the ROMC 03 -> 1A/1B latch
test_busio_CFLAGS = -DCHF_IO_LO=0x20 -DCHF_IO_HI=0x27

include ../../../common/host_test/cart.mk

all:: $(OUT)/test_romc
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
	    $(OUT)/test_romc $(BUILD)/trace/hello.trace   $(BUILD)/hello.bin \
	                     $(BUILD)/trace/romctest.trace $(BUILD)/romctest.bin; \
	fi
