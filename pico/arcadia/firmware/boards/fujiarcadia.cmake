# fujiarcadia: FujiNet Emerson Arcadia 2001 cartridge, first cut -- a plain
# Raspberry Pi Pico (RP2040) on a breadboard/adapter. The port needs 22 bus
# GPIOs (A0-A13, D0-D7); A12 doubles as the chip select, so there is no
# separate Enable pin. A future level-shifted PCB (the 5V TTL bus is not
# RP2040-tolerant) gets its own board file here.
set(PICO_PLATFORM rp2040)
set(PICO_FLASH_SIZE_BYTES 2097152)

option(CONFIG_FUJINET "Enable the FujiNet mailbox" ON)
