cmake_minimum_required(VERSION 3.13)

set(FUJI_DEFAULT_BOARD fuji7800_rp2354b)
include(${CMAKE_CURRENT_LIST_DIR}/../../common/cmake/fujicart_pre.cmake)

project(fuji7800 C CXX ASM)

pico_sdk_init()

# Everything runs from SRAM: core1 can never take an XIP miss, and the flash
# is free for the image store. CORE1 is checked after the link.
fujicart_firmware(${PROJECT_NAME}
    USB_PRODUCT "FujiNet Atari 7800 Cart"
    SOURCES
        src/main.c
        src/a78_cart.c
        src/a78_pio.c
        src/a78map.c
        src/a78map_db.c
        src/fuji_cart.c
        src/fuji_load.c
        ${FUJI_COMMON}/src/fuji_store.c
        src/fuji_audio.c
        src/pokey.c
        src/hsc.c
        ${FUJI_COMMON}/usb/usb_descriptors.c
        ${FUJI_COMMON}/src/fujibus.c
        ${FUJI_COMMON}/src/fujibus_usb.c
        ${FUJI_COMMON}/src/fujimail.c
        ${FUJI_COMMON}/src/fuji_cfg.c
        ${FUJI_COMMON}/src/fuji_port.c
        ${FUJI_COMMON}/src/fuji_watchdog.c
        src/fujinet.c
        src/fujiboot.c
    LIBRARIES hardware_pio hardware_pwm hardware_dma hardware_flash hardware_sync
    PIO src/a78_tables.pio
    COPY_TO_RAM
    CORE1 a78_core1_main fuji_live fuji_bus fuji_arena fuji_loader fuji_pokey_rd
          fuji_hsc_shadow a78_pio_patch a78_pio_load
)
