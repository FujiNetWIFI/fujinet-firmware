# The card's bus side and its core0 engine against fake_esp.h: the ROM's boot, the
# FD1771's commands, the EI's latches, the printer, the RS-232 board and the pipe.
CORE = $(SRC)/m1_card.c $(SRC)/m1_wd1771.c $(SRC)/m1_uart.c \
       $(COMMON)/src/fujibus.c $(COMMON)/src/fuji_iolink.c $(COMMON)/src/fuji_tcache.c
TESTS = test_rom_boot test_wd1771 test_card test_timing
$(foreach t,$(TESTS),$(eval $(t)_SRCS = $$(CORE)))

include ../../../common/host_test/cart.mk
