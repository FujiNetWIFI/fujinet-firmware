cmake_minimum_required(VERSION 3.13)

set(FUJI_DEFAULT_BOARD fujines_rp2354b)
include(${CMAKE_CURRENT_LIST_DIR}/../../common/cmake/fujicart_pre.cmake)

project(fujines C CXX ASM)

pico_sdk_init()

set(FUJI_COMMON ${CMAKE_CURRENT_LIST_DIR}/../../common)

add_executable(${PROJECT_NAME}
    src/main.c
    src/nes_cart.c
    src/nes_pio.c
    src/nes_irq.c
    src/nesmap.c
    src/fuji_cart.c
    src/fuji_store.c
    ${FUJI_COMMON}/usb/usb_descriptors.c
)

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/nes_tables.pio)

target_compile_options(${PROJECT_NAME} PRIVATE -Os -Wall)
target_compile_definitions(${PROJECT_NAME} PRIVATE FUJI_USB_PRODUCT="FujiNet NES Cart")

target_sources(${PROJECT_NAME} PRIVATE
    ${FUJI_COMMON}/src/fujibus.c
    ${FUJI_COMMON}/src/fujibus_usb.c
    ${FUJI_COMMON}/src/fujimail.c
    src/fujinet.c
    src/fujiboot.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${FUJI_COMMON}/include
    ${FUJI_COMMON}/usb
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_irq
    hardware_flash
    hardware_sync
    tinyusb_device
)

# Everything runs from SRAM: core1's bus loop can never take an XIP miss, and
# flash is free for the image store. tools/checksram.py checks the symbols.
pico_set_binary_type(${PROJECT_NAME} copy_to_ram)
pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 0)

pico_add_extra_outputs(${PROJECT_NAME})
