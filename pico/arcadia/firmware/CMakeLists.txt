cmake_minimum_required(VERSION 3.13)

set(FUJI_DEFAULT_BOARD fujiarcadia)
include(${CMAKE_CURRENT_LIST_DIR}/../../common/cmake/fujicart_pre.cmake)

project(fujiarcadia C CXX ASM)

pico_sdk_init()

fujicart_firmware(${PROJECT_NAME}
    USB_PRODUCT "FujiNet Arcadia 2001 Cart"
    SOURCES
        src/main.c
        src/arcadia_cart.c
        src/fuji_cart.c
        ${FUJI_COMMON}/usb/usb_descriptors.c
        ${FUJI_COMMON}/src/fujibus.c
        ${FUJI_COMMON}/src/fujibus_usb.c
        ${FUJI_COMMON}/src/fujimail.c
        ${FUJI_COMMON}/src/fuji_cfg.c
        ${FUJI_COMMON}/src/fuji_port.c
        src/fujinet.c
        src/fujiboot.c
        src/arcmap.c
        ${FUJI_COMMON}/src/fuji_tick.c
    LIBRARIES hardware_vreg
    CORE1 arcadia_core1_main
)
