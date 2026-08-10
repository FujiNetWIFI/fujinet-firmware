set(PICO_PLATFORM rp2040)
set(CONFIG_USB_DEVICE 1)
set(PICO_FLASH_SIZE_BYTES 16777216)
set(PICO_FLASH_SPI_CLKDIV 2)

option(CONFIG_SD_STORAGE "SD storage support" OFF)
option(CONFIG_FLASH_FAT_STORAGE "Flash FAT storage" ON)
option(CONFIG_FLASH_LFS_STORAGE "Flash LFS storage" OFF)
option(CONFIG_JLP "Enable JLP" ON)
option(CONFIG_ECS_AUDIO "Enable ECS audio" OFF)
option(CONFIG_INTELLIVOICE "Enable Intellivoice" OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
   set(MAX_ROM_SIZE 1024*100)  # ~200 kb 
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
   set(MAX_ROM_SIZE 1024*100)  # ~200 kb
endif()

