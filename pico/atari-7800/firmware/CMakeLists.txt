cmake_minimum_required(VERSION 3.13)

# -DPICO_BOARD=x pulls in boards/x.cmake; the SDK finds boards/x.h through
# PICO_BOARD_HEADER_DIRS. The CONFIG, loader and boot-block headers come from
# FUJI_GEN (build-cart.sh's fresh ones) if there, else from baked/.
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
if(NOT PICO_BOARD)
    set(PICO_BOARD fuji7800_rp2354b)
endif()
include(${CMAKE_CURRENT_LIST_DIR}/boards/${PICO_BOARD}.cmake)
if(NOT FUJI_GEN)
    set(FUJI_GEN ${CMAKE_CURRENT_LIST_DIR}/../build/gen)
endif()

include(pico_sdk_import.cmake)

project(fuji7800 C CXX ASM)

pico_sdk_init()

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
    src/usb_descriptors.c
    src/fujibus.c
    src/fujibus_usb.c
    src/fujimail.c
    src/fujinet.c
    src/fujiboot.c
)

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/a78_tables.pio)

target_compile_options(${PROJECT_NAME} PRIVATE -Os -Wall)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${FUJI_GEN}
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/baked
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
# is free for the image store. tools/checksram.py checks the symbols.
pico_set_binary_type(${PROJECT_NAME} copy_to_ram)
pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 0)

pico_add_extra_outputs(${PROJECT_NAME})
