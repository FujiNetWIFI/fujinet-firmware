# fujichannelf: FujiNet Channel F Videocart, first cut -- a plain Raspberry Pi
# Pico (RP2040) on a breadboard/adapter.
#
# The port needs 16 bus GPIOs (D0-D7, ROMC0-4, WRITE, PHI, and the data-buffer
# direction), which leaves ten of a stock Pico's 26 spare. The ColecoVision
# port needed all 26; this connector carries no address bus to consume them.
#
# Memory: a 16K ROM window kept in two buffers so a booted client can push
# again without overwriting itself, plus one 32K arena -- 64K of the RP2040's
# 264K. There is no image store and no flash tier: every Videocart ever made is
# 6K or less, so nothing needs one.
set(PICO_PLATFORM rp2040)
set(PICO_FLASH_SIZE_BYTES 2097152)

option(CONFIG_FUJINET "Enable the FujiNet mailbox" ON)
