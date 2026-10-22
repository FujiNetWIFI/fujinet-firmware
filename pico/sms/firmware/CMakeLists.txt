cmake_minimum_required(VERSION 3.13)

set(FUJI_DEFAULT_BOARD fujisms_rp2354b)
include(${CMAKE_CURRENT_LIST_DIR}/../../common/cmake/fujicart_pre.cmake)

project(fujisms C CXX ASM)

pico_sdk_init()

# Everything runs from SRAM: core1 can never take an XIP miss, and the flash
# is free for the image store. CORE1 is checked after the link.
fujicart_firmware(${PROJECT_NAME}
    USB_PRODUCT "FujiNet Master System Cart"
    SOURCES
        src/main.c
        src/sms_cart.c
        src/smsmap.c
        src/smsmap_db.c
        src/fuji_cart.c
        src/fuji_load.c
        src/fuji_store.c
        ${FUJI_COMMON}/usb/usb_descriptors.c
        ${FUJI_COMMON}/src/fujibus.c
        ${FUJI_COMMON}/src/fujibus_usb.c
        ${FUJI_COMMON}/src/fujimail.c
        src/fujinet.c
        src/fujiboot.c
    LIBRARIES hardware_pwm hardware_flash hardware_sync
    COPY_TO_RAM
    CORE1 sms_core1_main fuji_live fuji_bus fuji_ptab_resident
)
