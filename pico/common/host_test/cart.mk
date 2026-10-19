# cart.mk -- rules shared by every cart's host_test/Makefile: no hardware, no pico-sdk.
#
# Set TESTS, and <test>_SRCS / <test>_CFLAGS / <test>_DEPS where a test needs them, then
# include this. Binaries go to $(OUT), outside the source tree. A cart adds checks of its
# own with another `all::` rule after the include.

SHELL := /bin/bash
.DEFAULT_GOAL := all

COMMON := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))..)

CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror -O1 -g -std=c11
INC ?= -I. -I.. -I../include -I$(COMMON)/include
SRC ?= ../src
OUT ?= ../../build/host_test

HDRS = $(wildcard *.h ../include/*.h $(COMMON)/include/*.h)

all:: $(addprefix $(OUT)/,$(TESTS))
	@$(foreach t,$(TESTS),echo "== $(t)"; $(OUT)/$(t) || exit 1;)

$(OUT):
	mkdir -p $@

.SECONDEXPANSION:
$(OUT)/%: %.c $$($$*_SRCS) $$($$*_DEPS) $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $($*_CFLAGS) $(INC) -o $@ $< $($*_SRCS)

clean::
	rm -rf $(OUT)

.PHONY: all clean
