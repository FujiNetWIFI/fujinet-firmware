# fujicard: FujiNet Intellivision cartridge derived from pintycard (RP2354A,
# no SD, LittleFS flash), with an ESP32-S3 on the cart linked over USB.
set(PICO_PLATFORM rp2350)
set(CONFIG_SD_STORAGE 0)
set(PICO_FLASH_SIZE_BYTES 2097152)
set(PICO_FLASH_SPI_CLKDIV 2)

option(CONFIG_FLASH_FAT_STORAGE "Flash FAT storage" OFF)
option(CONFIG_FLASH_LFS_STORAGE "Flash LFS storage" ON)
option(CONFIG_JLP "Enable JLP" ON)
option(CONFIG_ECS_AUDIO "Enable ECS audio" OFF)
option(CONFIG_INTELLIVOICE "Enable Intellivoice" OFF)
option(CONFIG_FUJINET "Enable FujiNet mailbox" ON)

# The USB CDC link to the ESP32-S3 is needed in Release too, so both builds
# take pintycard's Debug MAX_ROM_SIZE to pay for the USB stack.
set(CONFIG_USB_DEVICE 1)
set(MAX_ROM_SIZE 1024*215)    # ~430 kb
