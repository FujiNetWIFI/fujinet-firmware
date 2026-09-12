# Host tests for the Atari 2600 FujiNet cartridge.
#
# Plain gcc, no SDK, no hardware, no emulator. This is where everything an
# emulator structurally cannot check lives: MAME hands the cart device a clean
# data byte on a write, so the data_prev sampling the real cartridge depends on
# is never exercised there, and the glyph compositor never runs there at all.
#
#   make        build and run everything
#   make clean

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Werror -std=c11
INC     := -I../include -I.
TOOLS   := ../../tools

TESTS   := test_render test_fujibus test_busio test_vcsmap

.PHONY: all clean
all: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

# The font table and the render golden are both generated from
# tools/vcsfont.py, so the C and the reference cannot drift apart by hand.
../include/vcs_font.h: $(TOOLS)/vcsfont.py $(TOOLS)/mkfont.py
	python3 $(TOOLS)/mkfont.py > $@

render_gold.h: $(TOOLS)/vcsfont.py $(TOOLS)/mkrendergold.py
	python3 $(TOOLS)/mkrendergold.py > $@

test_render: test_render.c ../src/vcs_render.c ../include/vcs_font.h render_gold.h
	$(CC) $(CFLAGS) $(INC) -o $@ test_render.c ../src/vcs_render.c

test_fujibus: test_fujibus.c ../src/fujibus.c
	$(CC) $(CFLAGS) $(INC) -o $@ test_fujibus.c ../src/fujibus.c

test_busio: test_busio.c
	$(CC) $(CFLAGS) $(INC) -o $@ test_busio.c

clean:
	rm -f $(TESTS) render_gold.h

test_vcsmap: test_vcsmap.c ../include/vcsmap.h ../include/vcs_cart.h
	$(CC) $(CFLAGS) $(INC) -o $@ test_vcsmap.c
