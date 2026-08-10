set(PICO_PLATFORM rp2040)
set(PICO_FLASH_SIZE_BYTES 16777216)
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
   set(MAX_ROM_SIZE 1024*106)  # 212 kb
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
   set(CONFIG_USB_DEVICE 1)
   set(MAX_ROM_SIZE 1024*90)  # 180 kb
endif()
