# fujicade_rp2354: the FujiNet-Astrocade Rev0 cartridge PCB
# (fujinet-hardware Astrocade/rev0) -- RP2354A (RP2350A die + 2 MB flash in
# the package) wired straight to the 5V bus through its 5V-tolerant pads,
# plus an ESP32-S3 that is this chip's USB host and can force RUN/QSPI_SS
# to reflash it over PICOBOOT. Same bus pin map as fujicade (GP0-12 address,
# GP13 /ENABLE, GP14-21 data); see boards/fujicade_rp2354.h.
set(PICO_PLATFORM rp2350)
set(PICO_FLASH_SIZE_BYTES 2097152)

option(CONFIG_FUJINET "Enable the FujiNet mailbox" ON)
