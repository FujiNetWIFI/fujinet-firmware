cmake_minimum_required(VERSION 3.13)

set(FUJI_DEFAULT_BOARD fujichannelf)
include(${CMAKE_CURRENT_LIST_DIR}/../../common/cmake/fujicart_pre.cmake)

project(fujichannelf C CXX ASM)

pico_sdk_init()

fujicart_firmware(${PROJECT_NAME}
    USB_PRODUCT "FujiNet Channel F Cart"
    SOURCES
        src/main.c
        src/channelf_cart.c
        src/fuji_cart.c
        ${FUJI_COMMON}/usb/usb_descriptors.c
        ${FUJI_COMMON}/src/fujibus.c
        ${FUJI_COMMON}/src/fujibus_usb.c
        ${FUJI_COMMON}/src/fujimail.c
        ${FUJI_COMMON}/src/fuji_cfg.c
        ${FUJI_COMMON}/src/fuji_port.c
        src/fujinet.c
        src/fujiboot.c
        ${FUJI_COMMON}/src/fuji_tick.c
        src/chfmap.c
    LIBRARIES hardware_vreg
    CORE1 channelf_core1_main fuji_store fuji_arena fuji_ring fuji_slot_npages fuji_live_win
          fuji_mailbox_active
    CORE1_NO_CALLS
)
