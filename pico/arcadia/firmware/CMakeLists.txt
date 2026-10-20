cmake_minimum_required(VERSION 3.13)

set(FUJI_DEFAULT_BOARD fujiarcadia)
include(${CMAKE_CURRENT_LIST_DIR}/../../common/cmake/fujicart_pre.cmake)

project(fujiarcadia C CXX ASM)

pico_sdk_init()

set(FUJI_COMMON ${CMAKE_CURRENT_LIST_DIR}/../../common)

add_executable(${PROJECT_NAME}
    src/main.c
    src/arcadia_cart.c
    src/fuji_cart.c
    ${FUJI_COMMON}/usb/usb_descriptors.c
)

target_compile_options(${PROJECT_NAME} PRIVATE -Os -Wall)
target_compile_definitions(${PROJECT_NAME} PRIVATE FUJI_USB_PRODUCT="FujiNet Arcadia 2001 Cart")

if(CONFIG_FUJINET)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CONFIG_FUJINET=1)
    target_sources(${PROJECT_NAME} PRIVATE
        ${FUJI_COMMON}/src/fujibus.c
        ${FUJI_COMMON}/src/fujibus_usb.c
        ${FUJI_COMMON}/src/fujimail.c
        src/fujinet.c
        src/fujiboot.c
        src/arcmap.c
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
    tinyusb_device
)

pico_add_extra_outputs(${PROJECT_NAME})
