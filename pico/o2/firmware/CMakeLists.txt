cmake_minimum_required(VERSION 3.13)

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(PicoPAC_cart)

set(PICO_BOARD_HEADER_DIRS ${CMAKE_SOURCE_DIR}) 
set(PICO_BOARD myboard)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(PicoPAC_cart)

# 16megs of flash on purple pico clones
target_compile_definitions(PicoPAC_cart PRIVATE
    PICO_FLASH_SIZE_BYTES=16777216
)

target_compile_options(PicoPAC_cart PRIVATE "-Os")

# FUJINET: on by default in this fork -- the FujiNet mailbox is the whole point
# of it. Build with -DCONFIG_FUJINET=OFF for a stock PicoPAC.
option(CONFIG_FUJINET "Enable the FujiNet mailbox" ON)
if(CONFIG_FUJINET)
    target_compile_definitions(PicoPAC_cart PRIVATE CONFIG_FUJINET=1)
endif()

#pico_set_linker_script(PicoPAC_cart ${CMAKE_CURRENT_SOURCE_DIR}/memmap_custom.ld)

target_sources(PicoPAC_cart PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/main.c
    ${CMAKE_CURRENT_LIST_DIR}/picopac_cart.c
    ${CMAKE_CURRENT_LIST_DIR}/msc_disk.c
    ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
    ${CMAKE_CURRENT_LIST_DIR}/fatfs_disk.c
    ${CMAKE_CURRENT_LIST_DIR}/flash_fs.c
    ${CMAKE_CURRENT_LIST_DIR}/fatfs/ff.c
    ${CMAKE_CURRENT_LIST_DIR}/fatfs/ffunicode.c
    ${CMAKE_CURRENT_LIST_DIR}/fatfs/diskio.c
) 

# FUJINET additions. Deliberately kept in src/ and include/ rather than mixed
# in with the vendored files, so an upstream merge stays a readable diff.
if(CONFIG_FUJINET)
target_sources(PicoPAC_cart PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/src/fujibus.c
    ${CMAKE_CURRENT_LIST_DIR}/src/fujibus_usb.c
    ${CMAKE_CURRENT_LIST_DIR}/src/fuji_cart.c
    ${CMAKE_CURRENT_LIST_DIR}/src/fujimail.c
    ${CMAKE_CURRENT_LIST_DIR}/src/fujinet.c
    ${CMAKE_CURRENT_LIST_DIR}/src/fujiboot.c
    ${CMAKE_CURRENT_LIST_DIR}/src/o2map.c
)
endif()

target_include_directories(PicoPAC_cart PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/fatfs
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# Enable usb output, disable uart output
pico_enable_stdio_usb(PicoPAC_cart 1)
pico_enable_stdio_uart(PicoPAC_cart 0)

# In addition to pico_stdlib required for common PicoSDK functionality, add dependency on tinyusb_device
# for TinyUSB device support
target_link_libraries(PicoPAC_cart PUBLIC pico_stdlib hardware_flash tinyusb_device pico_multicore)
#target_link_libraries(PicoPAC_cart PUBLIC pico_stdlib hardware_flash)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(PicoPAC_cart)


