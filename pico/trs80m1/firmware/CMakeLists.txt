cmake_minimum_required(VERSION 3.13)

set(FUJI_DEFAULT_BOARD fujitrs80m1_rp2354b)
include(${CMAKE_CURRENT_LIST_DIR}/../../common/cmake/fujicart_pre.cmake)

project(fujitrs80m1 C CXX ASM)

pico_sdk_init()

# Everything runs from SRAM, so core1 never waits on a flash cache miss, and its loop
# calls nothing: every bus handler is static inline in the headers.
fujicart_firmware(${PROJECT_NAME}
    USB_PRODUCT "FujiNet TRS-80 Model I"
    SOURCES
        src/main.c
        src/m1_card.c
        src/m1_wd1771.c
        src/m1_uart.c
        ${FUJI_COMMON}/usb/usb_descriptors.c
        ${FUJI_COMMON}/src/fujibus.c
        ${FUJI_COMMON}/src/fuji_iolink.c
        ${FUJI_COMMON}/src/fuji_tcache.c
    LIBRARIES hardware_flash
    COPY_TO_RAM
    OPTIMIZE -O2
    CORE1 m1_core1_main card
    CORE1_NO_CALLS
)
