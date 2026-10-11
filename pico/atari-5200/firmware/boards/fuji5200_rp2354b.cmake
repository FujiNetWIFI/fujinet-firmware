# fuji5200_rp2354b: the FujiNet Atari 5200 cartridge -- an RP2354B
# (RP2350B die + 2 MB flash in the package, QFN-80) on the console's 5 V bus,
# driving D0-D7 through a 74HCT541. Pin map in include/a52_cart.h.
set(PICO_PLATFORM rp2350)
set(PICO_FLASH_SIZE_BYTES 2097152)
