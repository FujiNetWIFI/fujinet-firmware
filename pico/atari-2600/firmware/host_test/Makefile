# What emulation cannot check: MAME hands the cart a clean byte on a write, so test_busio
# covers the data_prev sampling, and the glyph compositor never runs there at all.
# test_fujimail is pico/common's, against this cart's fuji_mailbox.h.
TOOLS = ../../tools
GEN = $(OUT)/gen

TESTS = test_render test_busio test_vcsmap test_fujimail test_pathjob
test_render_SRCS = $(SRC)/vcs_render.c
test_render_DEPS = $(GEN)/vcs_font.h $(GEN)/render_gold.h
test_fujimail_SRCS = $(COMMON)/src/fujimail.c
# The path ops as core1 runs them, a step at a time.
test_pathjob_CFLAGS = -DVCS_CART_DEFER_PATH
EXTRA_CFLAGS = -I$(GEN)

include ../../../common/host_test/cart.mk

# Both generated from tools/vcsfont.py, so the C and the reference cannot drift apart.
$(GEN)/vcs_font.h: $(TOOLS)/vcsfont.py $(TOOLS)/mkfont.py
	mkdir -p $(GEN)
	python3 $(TOOLS)/mkfont.py > $@

$(GEN)/render_gold.h: $(TOOLS)/vcsfont.py $(TOOLS)/mkrendergold.py
	mkdir -p $(GEN)
	python3 $(TOOLS)/mkrendergold.py > $@
