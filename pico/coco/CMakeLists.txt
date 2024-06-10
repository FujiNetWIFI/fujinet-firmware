# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
# "/home/jeffp/Projects/pico/pico-sdk"
set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})

set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.4.0")
  message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.4.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

project(main C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(main main.c rom.c)

# by default the header is generated into the build dir
# however, alternatively you can choose to generate it somewhere else (in this case in the source tree for check in)
# pico_generate_pio_header(pio_and ${CMAKE_CURRENT_LIST_DIR}/blink.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR})
pico_generate_pio_header(main ${CMAKE_CURRENT_LIST_DIR}/cococart.pio)
pico_generate_pio_header(main ${CMAKE_CURRENT_LIST_DIR}/uart_tx.pio)
pico_generate_pio_header(main ${CMAKE_CURRENT_LIST_DIR}/uart_rx.pio)

# target_sources(rom PRIVATE rom.c)

target_link_libraries(main pico_stdlib pico_multicore hardware_pio hardware_dma)

pico_enable_stdio_usb(main 1)
pico_enable_stdio_uart(main 0)

# Add the standard include files to the build
target_include_directories(main PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts or any other standard includes, if required
)

pico_add_extra_outputs(main)
