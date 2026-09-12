# fujivcs: FujiNet Atari 2600 cartridge, first cut -- a stock Raspberry Pi Pico
# (RP2040) behind level shifters.
#
# The port needs 22 bus GPIOs: A0-A12, D0-D7 and the data-buffer direction.
# That fits a stock Pico with room to spare, which PlusCart's pinout does not
# -- it puts the data byte on GP22-GP29 and needs the "purple" YD-RP2040. See
# include/vcs_pins.h for why the data wiring moved and the address wiring did
# not.
#
# Memory: two 32K image buffers, ping-ponged so a booted client can push again
# without overwriting the code it is running from, plus the 4K served window.
# 68K of the RP2040's 264K.
#
# NO FLASH TIER, and this is not an optimisation. The serve path has roughly
# 500 ns from address to data and a single XIP cache miss is most of it. The
# ColecoVision port reached the same conclusion with 373 ns. 32K is therefore
# the ceiling, which is also where MAME's cartridge whitelist and the
# (N+1)*2048 client layout meet.
set(PICO_PLATFORM rp2040)
set(PICO_FLASH_SIZE_BYTES 2097152)

option(CONFIG_FUJINET "Enable the FujiNet mailbox" ON)
