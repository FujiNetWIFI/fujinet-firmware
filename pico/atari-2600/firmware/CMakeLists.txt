cmake_minimum_required(VERSION 3.13)

set(FUJI_DEFAULT_BOARD fujivcs_rp2354a)
include(${CMAKE_CURRENT_LIST_DIR}/../../common/cmake/fujicart_pre.cmake)

project(fujivcs C CXX ASM)

pico_sdk_init()

# vcs_font.h is generated from tools/vcsfont.py, the same source the host tests' reference
# renderer reads, so the two cannot drift. Here rather than in build-cart.sh so a --baked
# build has it too.
set(VCS_TOOLS ${CMAKE_CURRENT_LIST_DIR}/../tools)
set(VCS_FONT_DIR ${CMAKE_CURRENT_BINARY_DIR}/vcsfont)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${VCS_TOOLS}/mkfont.py ${VCS_TOOLS}/vcsfont.py)
file(MAKE_DIRECTORY ${VCS_FONT_DIR})
execute_process(
    COMMAND python3 ${VCS_TOOLS}/mkfont.py
    OUTPUT_FILE ${VCS_FONT_DIR}/vcs_font.h
    RESULT_VARIABLE VCS_FONT_RC
)
if(NOT VCS_FONT_RC EQUAL 0)
    message(FATAL_ERROR "mkfont.py failed (${VCS_FONT_RC})")
endif()

fujicart_firmware(${PROJECT_NAME}
    USB_PRODUCT "FujiNet Atari 2600 Cart"
    SOURCES
        src/main.c
        src/vcs_cart.c
        src/fuji_cart.c
        src/vcs_render.c
        ${FUJI_COMMON}/usb/usb_descriptors.c
        ${FUJI_COMMON}/src/fujibus.c
        ${FUJI_COMMON}/src/fujibus_usb.c
        ${FUJI_COMMON}/src/fujimail.c
        ${FUJI_COMMON}/src/fuji_cfg.c
        ${FUJI_COMMON}/src/fuji_port.c
        ${FUJI_COMMON}/src/fuji_watchdog.c
        src/fujinet.c
        src/fujiboot.c
    # pico_usb_reset: 1200 baud on the CDC line reboots into BOOTSEL, which is how the
    # ESP32-S3 reflashes the cart
    LIBRARIES hardware_pwm hardware_vreg pico_usb_reset
    COPY_TO_RAM
    CORE1 vcs_core1_main fuji_cart_serve_staged fuji_mem fuji_ring fuji_image
)
target_include_directories(${PROJECT_NAME} PRIVATE ${VCS_FONT_DIR})
target_compile_definitions(${PROJECT_NAME} PRIVATE PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=1
    VCS_CART_DEFER_PATH=1)
