cmake_minimum_required(VERSION 3.13)

set(FUJI_DEFAULT_BOARD fujicoleco)
include(${CMAKE_CURRENT_LIST_DIR}/../../common/cmake/fujicart_pre.cmake)

project(fujicoleco C CXX ASM)

pico_sdk_init()

fujicart_firmware(${PROJECT_NAME}
    USB_PRODUCT "FujiNet ColecoVision Cart"
    SOURCES
        src/main.c
        src/coleco_cart.c
        src/fuji_cart.c
        ${FUJI_COMMON}/usb/usb_descriptors.c
        ${FUJI_COMMON}/src/fujibus.c
        ${FUJI_COMMON}/src/fujibus_usb.c
        ${FUJI_COMMON}/src/fujimail.c
        ${FUJI_COMMON}/src/fuji_cfg.c
        ${FUJI_COMMON}/src/fuji_port.c
        src/fujinet.c
        src/fujiboot.c
        src/fuji_store.c
        src/colmap.c
        ${FUJI_COMMON}/src/fuji_watchdog.c
    LIBRARIES hardware_vreg hardware_pwm hardware_watchdog
    CORE1 coleco_core1_main
)

# core1's serve path is forced inline into its SRAM-resident loop, so none of it can be
# left in flash; the CORE1 check above follows every call it makes.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    "COLECO_HOT=static inline __attribute__((always_inline))"
)
