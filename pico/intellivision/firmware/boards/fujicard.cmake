# fujicard: Minty-based FujiNet Intellivision cartridge, derived from
# pintycard (RP2354A, no SD, LittleFS flash storage). See HARDWARE.md in
# ~/Workspace/PiRTOII-Fuji for the board this targets: a single RP2040/
# RP2350 cart housing both an RP2040-class MCU and an ESP32-S3, linked
# internally over USB.
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

# Unlike pintycard, this board needs the USB CDC link to the ESP32-S3 live
# in Release too -- it's not just a debug convenience here, it's the whole
# point. MAX_ROM_SIZE is trimmed to the Debug figure in both configs to pay
# for the USB device stack that's now always compiled in.
set(CONFIG_USB_DEVICE 1)
set(MAX_ROM_SIZE 1024*215)    # ~430 kb
