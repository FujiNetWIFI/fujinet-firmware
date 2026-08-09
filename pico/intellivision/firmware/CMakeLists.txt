cmake_minimum_required(VERSION 3.13)

# PICO_BOARD / PICO_BOARD_HEADER_DIRS must be set before pico_sdk_import.cmake
# is included, not after: the SDK resolves boards/<PICO_BOARD>.h as part of
# that include, before project() even runs. Setting them afterwards (as the
# upstream PiRTO II CMakeLists did) silently falls back to the stock
# boards/pico.h and myboard.h's overrides -- notably the UART pin
# assignment that keeps the debug console off the CP-1610 bus pins -- are
# never applied.
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR})
set(PICO_BOARD myboard)

# initialize the SDK based on PICO_SDK_PATH (set PICO_SDK_PATH in your
# environment, e.g. /usr/share/pico-sdk -- this must happen before project())
include(pico_sdk_import.cmake)

project(fuji_intv)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(fuji_intv)

# 16megs of flash on purple pico clones
target_compile_definitions(fuji_intv PRIVATE
    PICO_FLASH_SIZE_BYTES=16777216
)

target_compile_options(fuji_intv PRIVATE "-Os")

# msc_disk.c/fatfs_disk.c/flash_fs.c/fatfs/ (USB-MSC ROM loading + the local
# flash filesystem browser) were removed with the PiRTOII-Fuji board: with no
# external RP2040 USB port, there's no way to write into that filesystem
# anymore, and dropping it frees ~66KB of SRAM (see the SCOPE NOTE in
# inty_cart.c). ROMs now arrive only over the network FujiBus link.
target_sources(fuji_intv PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/main.c
    ${CMAKE_CURRENT_LIST_DIR}/inty_cart.c
    ${CMAKE_CURRENT_LIST_DIR}/fujibus.c
    ${CMAKE_CURRENT_LIST_DIR}/fujibus_usb.c
    ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
)

target_include_directories(fuji_intv PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

# Debug console goes out over UART0/GPIO28 (see myboard.h). stdio must NOT go
# over the USB CDC link -- that link is the FujiBus wire to the ESP32-S3, and
# any stray byte before a SLIP frame gets forwarded to fujinet's modem device
# (lib/bus/rs232/rs232.cpp).
pico_enable_stdio_usb(fuji_intv 0)
pico_enable_stdio_uart(fuji_intv 1)

# In addition to pico_stdlib required for common PicoSDK functionality, add dependency on tinyusb_device
# for TinyUSB device support
target_link_libraries(fuji_intv PUBLIC pico_stdlib tinyusb_device pico_multicore)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(fuji_intv)
