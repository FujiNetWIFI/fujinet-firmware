# fujicoleco: FujiNet ColecoVision cartridge, first cut -- a plain Raspberry Pi
# Pico (RP2040) on a breadboard/adapter.
#
# The port needs 25 bus GPIOs (A0-A14, D0-D7, the combined chip select, the
# data-buffer direction) plus one for console power sense, which is exactly a
# stock Pico's 26. There is no pin left over, and that is a deliberate trade:
# power sense buys both the way back to CONFIG after a game boot and the
# safe-with-console-off buffer default, which a mode button would not.
#
# 128K of the RP2040's 264K SRAM is the image store. There is no flash tier --
# see fuji_store.h; a 3.58 MHz Z80 does not leave room for an XIP cache miss.
# Lifting the 128K image ceiling (the 256K/512K MegaCarts, both X-in-1 images)
# means an RP2350 board, which would get its own file here.
set(PICO_PLATFORM rp2040)
set(PICO_FLASH_SIZE_BYTES 2097152)

option(CONFIG_FUJINET "Enable the FujiNet mailbox" ON)
