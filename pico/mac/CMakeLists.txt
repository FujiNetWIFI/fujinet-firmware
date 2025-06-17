# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

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

project(commands C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(commands commands.c)

# by default the header is generated into the build dir
pico_generate_pio_header(commands ${CMAKE_CURRENT_LIST_DIR}/commands.pio)
pico_generate_pio_header(commands ${CMAKE_CURRENT_LIST_DIR}/enand.pio)
pico_generate_pio_header(commands ${CMAKE_CURRENT_LIST_DIR}/echo.pio)
pico_generate_pio_header(commands ${CMAKE_CURRENT_LIST_DIR}/latch.pio)
pico_generate_pio_header(commands ${CMAKE_CURRENT_LIST_DIR}/mux.pio)
pico_generate_pio_header(commands ${CMAKE_CURRENT_LIST_DIR}/dcd_commands.pio)
pico_generate_pio_header(commands ${CMAKE_CURRENT_LIST_DIR}/dcd_read.pio)
pico_generate_pio_header(commands ${CMAKE_CURRENT_LIST_DIR}/dcd_write.pio)
# however, alternatively you can choose to generate it somewhere else (in this case in the source tree for check in)
#pico_generate_pio_header(pio_and ${CMAKE_CURRENT_LIST_DIR}/blink.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR})

# target_sources(pio_and PRIVATE pio_and.c)

target_link_libraries(commands pico_stdlib hardware_pio hardware_dma)

# Add the standard include files to the build
target_include_directories(commands PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts or any other standard includes, if required
)

pico_add_extra_outputs(commands)
