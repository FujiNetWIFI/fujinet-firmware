# fujichannelf: FujiNet Channel F Videocart on a plain Raspberry Pi Pico
# (RP2040). 16 bus GPIOs: D0-D7, ROMC0-4, WRITE, PHI and the buffer direction.
#
# Memory: two 64K image slots (a booted client can stage again without
# overwriting itself) plus a 32K arena; no flash tier.
set(PICO_PLATFORM rp2040)
set(PICO_FLASH_SIZE_BYTES 2097152)
