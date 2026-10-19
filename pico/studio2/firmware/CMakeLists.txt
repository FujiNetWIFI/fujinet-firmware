cmake_minimum_required(VERSION 3.13)

set(FUJI_DEFAULT_BOARD fujistudio2_rp2354b)
include(${CMAKE_CURRENT_LIST_DIR}/../../common/cmake/fujicart_pre.cmake)

project(fujistudio2 C CXX ASM)

pico_sdk_init()

# FujiBus and the USB device, shared with the other carts.
set(FUJI_COMMON ${CMAKE_CURRENT_LIST_DIR}/../../common)

add_executable(${PROJECT_NAME}
    src/main.c
    src/s2_cart.c
    src/s2map.c
    src/s2_text.c
    src/s2_font.c
    src/fuji_cart.c
    src/fuji_load.c
    ${FUJI_COMMON}/usb/usb_descriptors.c
    ${FUJI_COMMON}/src/fujibus.c
    ${FUJI_COMMON}/src/fujibus_usb.c
    ${FUJI_COMMON}/src/fujimail.c
    src/fujinet.c
)

target_compile_options(${PROJECT_NAME} PRIVATE -Os -Wall)
target_compile_definitions(${PROJECT_NAME} PRIVATE FUJI_USB_PRODUCT="FujiNet Studio II Cart")

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
    ${FUJI_COMMON}/include
    ${FUJI_COMMON}/usb
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
