cmake_minimum_required(VERSION 3.13)

set(FUJI_DEFAULT_BOARD fujichannelf)
include(${CMAKE_CURRENT_LIST_DIR}/../../common/cmake/fujicart_pre.cmake)

project(fujichannelf C CXX ASM)

pico_sdk_init()

# FujiBus and the USB device, shared with the other carts.
set(FUJI_COMMON ${CMAKE_CURRENT_LIST_DIR}/../../common)

add_executable(${PROJECT_NAME}
    src/main.c
    src/channelf_cart.c
    src/fuji_cart.c
    ${FUJI_COMMON}/usb/usb_descriptors.c
)

target_compile_options(${PROJECT_NAME} PRIVATE -Os -Wall)
target_compile_definitions(${PROJECT_NAME} PRIVATE FUJI_USB_PRODUCT="FujiNet Channel F Cart")

if(CONFIG_FUJINET)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CONFIG_FUJINET=1)
    target_sources(${PROJECT_NAME} PRIVATE
        ${FUJI_COMMON}/src/fujibus.c
        ${FUJI_COMMON}/src/fujibus_usb.c
        ${FUJI_COMMON}/src/fujimail.c
        src/fujinet.c
        src/fujiboot.c
        src/fuji_tick.c
        src/chfmap.c
    )
endif()

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
    hardware_vreg
    tinyusb_device
)

pico_add_extra_outputs(${PROJECT_NAME})

include(${FUJI_COMMON}/cmake/fujicart_checksram.cmake)
fujicart_checksram(${PROJECT_NAME} channelf_core1_main
    fuji_store fuji_arena fuji_ring fuji_slot_npages fuji_live_win fuji_mailbox_active
    NO_CALLS)
