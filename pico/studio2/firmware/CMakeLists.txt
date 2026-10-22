cmake_minimum_required(VERSION 3.13)

set(FUJI_DEFAULT_BOARD fujistudio2_rp2354b)
include(${CMAKE_CURRENT_LIST_DIR}/../../common/cmake/fujicart_pre.cmake)

project(fujistudio2 C CXX ASM)

pico_sdk_init()

fujicart_firmware(${PROJECT_NAME}
    USB_PRODUCT "FujiNet Studio II Cart"
    SOURCES
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
        ${FUJI_COMMON}/src/fuji_cfg.c
        ${FUJI_COMMON}/src/fuji_port.c
        src/fujinet.c
    LIBRARIES hardware_sync
    CORE1 s2_core1_main fuji_loader fuji_arena fuji_ring fuji_text fuji_bus
)

# core1's hot path is forced inline into its SRAM-resident loop, so none of
# it can be left in flash; the CORE1 check above follows every call it makes.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    "S2_HOT=static inline __attribute__((always_inline))"
)
