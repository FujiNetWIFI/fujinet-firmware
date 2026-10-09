cmake_minimum_required(VERSION 3.13)

# Board selection, as in the sibling ports: -DPICO_BOARD=x pulls in
# boards/x.cmake (platform, flash size, feature options) and the SDK finds
# boards/x.h through PICO_BOARD_HEADER_DIRS.
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
if(NOT PICO_BOARD)
    set(PICO_BOARD fujines_rp2354b)
endif()
include(${CMAKE_CURRENT_LIST_DIR}/boards/${PICO_BOARD}.cmake)

include(pico_sdk_import.cmake)

project(fujines C CXX ASM)

pico_sdk_init()

add_executable(${PROJECT_NAME}
    src/main.c
    src/nes_cart.c
    src/nes_pio.c
    src/nes_irq.c
    src/nesmap.c
    src/fuji_cart.c
    src/fuji_store.c
    src/usb_descriptors.c
)

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/nes_tables.pio)

target_compile_options(${PROJECT_NAME} PRIVATE -Os -Wall)

if(CONFIG_FUJINET)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CONFIG_FUJINET=1)
    target_sources(${PROJECT_NAME} PRIVATE
        src/fujibus.c
        src/fujibus_usb.c
        src/fujimail.c
        src/fujinet.c
        src/fujiboot.c
    )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
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
