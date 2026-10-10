cmake_minimum_required(VERSION 3.13)

# -DPICO_BOARD=x pulls in boards/x.cmake; the SDK finds boards/x.h through
# PICO_BOARD_HEADER_DIRS. The CONFIG header comes from FUJI_GEN
# (build-cart.sh's fresh one) if there, else from baked/.
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
if(NOT PICO_BOARD)
    set(PICO_BOARD fujistudio2_rp2354b)
endif()
include(${CMAKE_CURRENT_LIST_DIR}/boards/${PICO_BOARD}.cmake)
if(NOT FUJI_GEN)
    set(FUJI_GEN ${CMAKE_CURRENT_LIST_DIR}/../build/gen)
endif()

include(pico_sdk_import.cmake)

project(fujistudio2 C CXX ASM)

pico_sdk_init()

add_executable(${PROJECT_NAME}
    src/main.c
    src/s2_cart.c
    src/s2map.c
    src/s2_text.c
    src/s2_font.c
    src/fuji_cart.c
    src/fuji_load.c
    src/usb_descriptors.c
    src/fujibus.c
    src/fujibus_usb.c
    src/fujimail.c
    src/fujinet.c
)

target_compile_options(${PROJECT_NAME} PRIVATE -Os -Wall)

# core1's hot path is forced inline into its SRAM-resident loop, so none of
# it can be left in flash; tools/checksram.py checks every call it makes.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    "S2_HOT=static inline __attribute__((always_inline))"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${FUJI_GEN}
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/baked
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    pico_stdlib
    pico_multicore
    hardware_sync
    tinyusb_device
)

pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 0)

pico_add_extra_outputs(${PROJECT_NAME})
