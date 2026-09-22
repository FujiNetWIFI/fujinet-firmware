# fujicade: FujiNet Bally Astrocade cartridge, first cut -- a plain
# Raspberry Pi Pico (RP2040) on a breadboard/adapter. The port needs only 22
# bus GPIOs (A0-A12, D0-D7, /ENABLE), so unlike the Intellivision and O2
# boards there is pin budget to spare; a future level-shifted PCB gets its
# own board file here.
set(PICO_PLATFORM rp2040)
set(PICO_FLASH_SIZE_BYTES 2097152)

option(CONFIG_FUJINET "Enable the FujiNet mailbox" ON)
