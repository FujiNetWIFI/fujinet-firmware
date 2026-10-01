# fujines_rp2354b: the FujiNet NES cartridge -- an RP2354B (RP2350B die +
# 2 MB flash in the package, QFN-80, 48 GPIO) on the console's 5 V bus with
# two 512K x 8 SRAMs behind PIO bank tables. Pin map in include/nes_cart.h;
# the '595 carries the slow control bits. See boards/fujines_rp2354b.h.
set(PICO_PLATFORM rp2350)
set(PICO_FLASH_SIZE_BYTES 2097152)

option(CONFIG_FUJINET "Enable the FujiNet mailbox" ON)
