# fujiarcadia: Arcadia 2001 cart on a stock Pico (RP2040). 22 bus GPIOs (A0-A13, D0-D7);
# A12 is the chip select. The 5V bus is not RP2040-tolerant; a level-shifted PCB gets its own file.
set(PICO_PLATFORM rp2040)
set(PICO_FLASH_SIZE_BYTES 2097152)
