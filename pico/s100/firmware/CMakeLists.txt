cmake_minimum_required(VERSION 3.13)

# -DPICO_BOARD=x pulls in boards/x.cmake; the SDK finds boards/x.h through
# PICO_BOARD_HEADER_DIRS. The boot page and images come from FUJI_GEN
# (build-cart.sh's fresh ones) if there, else from baked/.
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
if(NOT PICO_BOARD)
    set(PICO_BOARD fujis100_rp2354b)
endif()
include(${CMAKE_CURRENT_LIST_DIR}/boards/${PICO_BOARD}.cmake)
if(NOT FUJI_GEN)
    set(FUJI_GEN ${CMAKE_CURRENT_LIST_DIR}/../build/gen)
endif()

include(pico_sdk_import.cmake)

project(fujis100 C CXX ASM)

pico_sdk_init()

# FujiBus codec and the USB device, shared with the carts.
set(FUJI_COMMON ${CMAKE_CURRENT_LIST_DIR}/../../common)

add_executable(${PROJECT_NAME}
    src/main.c
    src/s100_regs.c
    src/s100_fdc.c
    src/s100_2sio.c
    src/s100_link.c
    ${FUJI_COMMON}/usb/usb_descriptors.c
    ${FUJI_COMMON}/src/fujibus.c
)

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/s100_bus.pio)

target_compile_options(${PROJECT_NAME} PRIVATE -O2 -Wall)
target_compile_definitions(${PROJECT_NAME} PRIVATE FUJI_USB_PRODUCT="FujiNet S-100")

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
    hardware_pio
    tinyusb_device
)

# Everything runs from SRAM, so core1 never waits on a flash cache miss.
pico_set_binary_type(${PROJECT_NAME} copy_to_ram)

pico_enable_stdio_uart(${PROJECT_NAME} 0)
pico_enable_stdio_usb(${PROJECT_NAME} 0)

pico_add_extra_outputs(${PROJECT_NAME})
