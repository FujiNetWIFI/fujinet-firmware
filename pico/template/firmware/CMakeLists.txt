cmake_minimum_required(VERSION 3.13)

set(FUJI_DEFAULT_BOARD fujitemplate_rp2354b)
include(${CMAKE_CURRENT_LIST_DIR}/../../common/cmake/fujicart_pre.cmake)

project(fujitemplate C CXX ASM)

pico_sdk_init()

fujicart_firmware(${PROJECT_NAME}
    USB_PRODUCT "FujiNet Template Cart"
    SOURCES
        src/main.c
        src/tmpl_cart.c
        src/fuji_cart.c
        src/tmplmap.c
        ${FUJI_COMMON}/usb/usb_descriptors.c
        ${FUJI_COMMON}/src/fujibus.c
        ${FUJI_COMMON}/src/fujibus_usb.c
        ${FUJI_COMMON}/src/fujimail.c
        ${FUJI_COMMON}/src/fuji_cfg.c
        ${FUJI_COMMON}/src/fuji_port.c
        src/fujinet.c
        src/fujiboot.c
    CORE1 tmpl_core1_main
)
