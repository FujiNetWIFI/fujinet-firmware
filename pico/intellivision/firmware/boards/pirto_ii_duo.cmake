set(PICO_PLATFORM rp2350)
set(PICO_RP2350_A2_SUPPORTED 1)
set(PICO_FLASH_SIZE_BYTES 4194304)
set(PICO_FLASH_SPI_CLKDIV 2)

option(CONFIG_USB_DEVICE "USB device support" ON)
option(CONFIG_SD_STORAGE "SD storage support" ON)
option(CONFIG_FLASH_FAT_STORAGE "Flash FAT storage" OFF)
option(CONFIG_FLASH_LFS_STORAGE "Flash LFS storage" OFF)
option(CONFIG_JLP "Enable JLP" ON)
option(CONFIG_ECS_AUDIO "Enable ECS audio" OFF)
option(CONFIG_INTELLIVOICE "Enable Intellivoice" OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
   set(CONFIG_USB_DEVICE 0)
   set(MAX_ROM_SIZE 1024*228)    # ~456 kb
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
   set(CONFIG_USB_DEVICE 1)
   set(MAX_ROM_SIZE 1024*225)    # ~450 kb
endif()
