cmake_minimum_required(VERSION 3.13)

set(FUJI_DEFAULT_BOARD fujis100_rp2354b)
include(${CMAKE_CURRENT_LIST_DIR}/../../common/cmake/fujicart_pre.cmake)

project(fujis100 C CXX ASM)

pico_sdk_init()

# Everything runs from SRAM, so core1 never waits on a flash cache miss.
fujicart_firmware(${PROJECT_NAME}
    USB_PRODUCT "FujiNet S-100"
    SOURCES
        src/main.c
        src/s100_regs.c
        src/s100_fdc.c
        src/s100_2sio.c
        src/s100_link.c
        ${FUJI_COMMON}/usb/usb_descriptors.c
        ${FUJI_COMMON}/src/fujibus.c
    LIBRARIES hardware_pio
    PIO src/s100_bus.pio
    COPY_TO_RAM
    OPTIMIZE -O2
    CORE1 s100_core1_main card
)
