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

add_executable(${PROJECT_NAME}
    src/main.c
    src/astrocade_cart.c
    src/fuji_cart.c
    src/usb_descriptors.c
)

target_compile_options(${PROJECT_NAME} PRIVATE -Os -Wall)

if(CONFIG_FUJINET)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CONFIG_FUJINET=1)
    target_sources(${PROJECT_NAME} PRIVATE
        src/fujibus.c
        src/fujibus_usb.c
        src/fujimail.c
        src/fujinet.c
        src/fujiboot.c
        src/astromap.c
    )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    pico_stdlib
    pico_multicore
    hardware_vreg
    tinyusb_device
)

pico_add_extra_outputs(${PROJECT_NAME})
