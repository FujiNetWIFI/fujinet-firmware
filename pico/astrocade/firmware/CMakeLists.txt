cmake_minimum_required(VERSION 3.13)

# Board selection, Minty-style (pico/intellivision/firmware): -DPICO_BOARD=x
# pulls in boards/x.cmake (feature options, platform, flash size) and the SDK
# finds boards/x.h through PICO_BOARD_HEADER_DIRS.
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
if(NOT PICO_BOARD)
    set(PICO_BOARD fujicade)
endif()
include(${CMAKE_CURRENT_LIST_DIR}/boards/${PICO_BOARD}.cmake)

include(pico_sdk_import.cmake)

project(fujicade C CXX ASM)

pico_sdk_init()

# FujiBus and the USB device, shared with the other carts.
set(FUJI_COMMON ${CMAKE_CURRENT_LIST_DIR}/../../common)

add_executable(${PROJECT_NAME}
    src/main.c
    src/astrocade_cart.c
    src/fuji_cart.c
    ${FUJI_COMMON}/usb/usb_descriptors.c
)

target_compile_options(${PROJECT_NAME} PRIVATE -Os -Wall)
target_compile_definitions(${PROJECT_NAME} PRIVATE FUJI_USB_PRODUCT="FujiNet Astrocade Cart")

if(CONFIG_FUJINET)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CONFIG_FUJINET=1)
    target_sources(${PROJECT_NAME} PRIVATE
        ${FUJI_COMMON}/src/fujibus.c
        ${FUJI_COMMON}/src/fujibus_usb.c
        src/fujimail.c
        src/fujinet.c
        src/fujiboot.c
        src/fuji_store.c
        src/astromap.c
    )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${FUJI_COMMON}/include
    ${FUJI_COMMON}/usb
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    pico_stdlib
    pico_multicore
    hardware_vreg
    hardware_flash
    hardware_sync
    tinyusb_device
)

pico_add_extra_outputs(${PROJECT_NAME})
