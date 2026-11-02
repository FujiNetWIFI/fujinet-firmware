# fujio2_rp2354b: the FujiNet-O2 Rev0 cartridge (fujinet-hardware Magnavox-Odyssey2/FujiNet-O2-Rev0)
# -- an RP2354B (RP2350B die + 2 MB flash in the package, QFN-80) on the console's 5 V bus, every
# line through 330R into a 5 V-tolerant pad, on PicoPAC's pin map (picopac_cart.c).
set(PICO_PLATFORM rp2350)
set(PICO_FLASH_SIZE_BYTES 2097152)
