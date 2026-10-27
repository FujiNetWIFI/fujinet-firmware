cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})

set(PICO_BOARD pico CACHE STRING "Board type")

# must be before project()
include(pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.4.0")
  message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.4.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

project(commands C CXX ASM)

pico_sdk_init()

add_executable(commands commands.c)

pico_generate_pio_header(commands ${CMAKE_CURRENT_LIST_DIR}/commands.pio)
pico_generate_pio_header(commands ${CMAKE_CURRENT_LIST_DIR}/enand.pio)
pico_generate_pio_header(commands ${CMAKE_CURRENT_LIST_DIR}/echo.pio)
pico_generate_pio_header(commands ${CMAKE_CURRENT_LIST_DIR}/latch.pio)
pico_generate_pio_header(commands ${CMAKE_CURRENT_LIST_DIR}/mux.pio)
pico_generate_pio_header(commands ${CMAKE_CURRENT_LIST_DIR}/dcd_commands.pio)
pico_generate_pio_header(commands ${CMAKE_CURRENT_LIST_DIR}/dcd_read.pio)
pico_generate_pio_header(commands ${CMAKE_CURRENT_LIST_DIR}/dcd_write.pio)
pico_generate_pio_header(commands ${CMAKE_CURRENT_LIST_DIR}/gcr_capture.pio)
target_link_libraries(commands pico_stdlib hardware_pio hardware_dma)

target_include_directories(commands PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/..
)

# debug output on the Pico's USB port as well as UART0
pico_enable_stdio_usb(commands 1)
pico_enable_stdio_uart(commands 1)

pico_add_extra_outputs(commands)
