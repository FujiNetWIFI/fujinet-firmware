cmake_minimum_required(VERSION 3.13)

set(FUJI_DEFAULT_BOARD fuji5200_rp2354b)
include(${CMAKE_CURRENT_LIST_DIR}/../../common/cmake/fujicart_pre.cmake)

project(fuji5200 C CXX ASM)

pico_sdk_init()

fujicart_firmware(${PROJECT_NAME}
    USB_PRODUCT "FujiNet Atari 5200 Cart"
    SOURCES
        src/main.c
        src/a52_cart.c
        src/a52map.c
        src/a52map_db.c
        src/fuji_cart.c
        src/fuji_load.c
        ${FUJI_COMMON}/usb/usb_descriptors.c
        ${FUJI_COMMON}/src/fujibus.c
        ${FUJI_COMMON}/src/fujibus_usb.c
        ${FUJI_COMMON}/src/fujimail.c
        src/fujinet.c
    LIBRARIES hardware_flash hardware_sync
    CORE1 a52_core1_main fuji_loader fuji_arena fuji_ring
)

# core1's hot path is forced inline into its SRAM-resident loop, so none of
# it can be left in flash; the CORE1 check above follows every call it makes.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    "A52_HOT=static inline __attribute__((always_inline))"
)
