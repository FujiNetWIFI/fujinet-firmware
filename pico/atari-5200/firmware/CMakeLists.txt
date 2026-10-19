cmake_minimum_required(VERSION 3.13)

set(FUJI_DEFAULT_BOARD fuji5200_rp2354b)
include(${CMAKE_CURRENT_LIST_DIR}/../../common/cmake/fujicart_pre.cmake)

project(fuji5200 C CXX ASM)

pico_sdk_init()

add_executable(${PROJECT_NAME}
    src/main.c
    src/a52_cart.c
    src/a52map.c
    src/a52map_db.c
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
    "A52_HOT=static inline __attribute__((always_inline))"
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
    hardware_flash
    hardware_sync
    tinyusb_device
)

pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 0)

pico_add_extra_outputs(${PROJECT_NAME})
