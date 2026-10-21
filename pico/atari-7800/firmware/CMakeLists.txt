cmake_minimum_required(VERSION 3.13)

set(FUJI_DEFAULT_BOARD fuji7800_rp2354b)
include(${CMAKE_CURRENT_LIST_DIR}/../../common/cmake/fujicart_pre.cmake)

project(fuji7800 C CXX ASM)

pico_sdk_init()

set(FUJI_COMMON ${CMAKE_CURRENT_LIST_DIR}/../../common)

add_executable(${PROJECT_NAME}
    src/main.c
    src/a78_cart.c
    src/a78_pio.c
    src/a78map.c
    src/a78map_db.c
    src/fuji_cart.c
    src/fuji_load.c
    src/fuji_store.c
    src/fuji_audio.c
    src/pokey.c
    src/hsc.c
    ${FUJI_COMMON}/usb/usb_descriptors.c
    ${FUJI_COMMON}/src/fujibus.c
    ${FUJI_COMMON}/src/fujibus_usb.c
    ${FUJI_COMMON}/src/fujimail.c
    src/fujinet.c
    src/fujiboot.c
)

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/a78_tables.pio)

target_compile_options(${PROJECT_NAME} PRIVATE -Os -Wall)
target_compile_definitions(${PROJECT_NAME} PRIVATE FUJI_USB_PRODUCT="FujiNet Atari 7800 Cart")

target_include_directories(${PROJECT_NAME} PRIVATE
    ${FUJI_GEN}
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/baked
    ${FUJI_COMMON}/include
    ${FUJI_COMMON}/usb
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_pwm
    hardware_dma
    hardware_flash
    hardware_sync
    tinyusb_device
)

# Everything runs from SRAM: core1 can never take an XIP miss, and the flash
# is free for the image store. fujicart_checksram() below checks it.
pico_set_binary_type(${PROJECT_NAME} copy_to_ram)
pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 0)

pico_add_extra_outputs(${PROJECT_NAME})

include(${FUJI_COMMON}/cmake/fujicart_checksram.cmake)
fujicart_checksram(${PROJECT_NAME} a78_core1_main fuji_live fuji_bus fuji_arena fuji_loader
    fuji_pokey_rd fuji_hsc_shadow a78_pio_patch a78_pio_load)
