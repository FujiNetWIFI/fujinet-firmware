cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD waveshare_rp2350_pizero)

if(DEFINED ENV{PICO_SDK_PATH})
   include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)
else()
   include(pico_sdk_import.cmake)
endif()

set(PROJECT ay-3-8910)

project(${PROJECT} C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(${PROJECT})

target_include_directories (${PROJECT} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(${PROJECT} PUBLIC
   ${CMAKE_CURRENT_LIST_DIR}/emu2149.c
   ${CMAKE_CURRENT_LIST_DIR}/main.c
)

target_link_libraries(${PROJECT}
   pico_stdlib
   hardware_pwm
   pico_multicore
)

set_property(TARGET ${PROJECT} APPEND_STRING PROPERTY LINK_FLAGS "-Wl,--print-memory-usage")

pico_enable_stdio_usb(${PROJECT} 1)
pico_enable_stdio_uart(${PROJECT} 0)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(${PROJECT})



