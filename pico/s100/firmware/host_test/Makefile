# The FDC+ driven the way Altair code drives it, the link mux against fake_esp.h, the pipe,
# 2SIO and boot page registers, and the decode, wait-state and timing glue.
EXTRA_CFLAGS = -D_DEFAULT_SOURCE

CORE = $(SRC)/s100_regs.c $(SRC)/s100_fdc.c $(SRC)/s100_2sio.c $(SRC)/s100_link.c \
       $(COMMON)/src/fujibus.c
CORE_TESTS = test_regs test_2sio test_fdc test_link test_glue test_timing
TESTS = $(CORE_TESTS)
$(foreach t,$(CORE_TESTS),$(eval $(t)_SRCS = $$(CORE)))

# test_pio needs pioasm: on PATH, or the one build-cart.sh built.
PIOASM ?= $(shell command -v pioasm || ls ../../build/fw-*/pioasm-install/pioasm/pioasm 2>/dev/null | head -1)
ifneq ($(PIOASM),)
TESTS += test_pio
endif
test_pio_DEPS = $(OUT)/s100_bus.pio.h
test_pio_CFLAGS = -I$(OUT)

include ../../../common/host_test/cart.mk

$(OUT)/s100_bus.pio.h: $(SRC)/s100_bus.pio | $(OUT)
	$(PIOASM) -o c-sdk $< $@

all::
	@[ -n "$(PIOASM)" ] || echo "test_pio: SKIP (no pioasm; run ../../build-cart.sh once)"
