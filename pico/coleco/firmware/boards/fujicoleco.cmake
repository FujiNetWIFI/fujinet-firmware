# fujicoleco: ColecoVision cart on a stock Pico (RP2040). 25 bus GPIOs (A0-A14, D0-D7, the
# ANDed chip select, the buffer direction) plus console power sense: all 26 a Pico has.
# 128K of SRAM is the image store; bigger MegaCarts and X-in-1 need an RP2350 board.
set(PICO_PLATFORM rp2040)
set(PICO_FLASH_SIZE_BYTES 2097152)
