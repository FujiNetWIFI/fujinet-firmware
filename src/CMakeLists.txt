cmake_minimum_required(VERSION 3.16.0)

set(INCLUDES 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/pinmap

    ${CMAKE_SOURCE_DIR}/lib/bus
    ${CMAKE_SOURCE_DIR}/lib/bus/adamnet
    ${CMAKE_SOURCE_DIR}/lib/bus/comlynx
    ${CMAKE_SOURCE_DIR}/lib/bus/cx16_i2c
    ${CMAKE_SOURCE_DIR}/lib/bus/drivewire
    ${CMAKE_SOURCE_DIR}/lib/bus/h89
    ${CMAKE_SOURCE_DIR}/lib/bus/iec
    ${CMAKE_SOURCE_DIR}/lib/bus/iwm
    ${CMAKE_SOURCE_DIR}/lib/bus/mac
    ${CMAKE_SOURCE_DIR}/lib/bus/rc2014bus
    ${CMAKE_SOURCE_DIR}/lib/bus/rc2014sio
    ${CMAKE_SOURCE_DIR}/lib/bus/rs232
    ${CMAKE_SOURCE_DIR}/lib/bus/s100spi
    ${CMAKE_SOURCE_DIR}/lib/bus/sio

    ${CMAKE_SOURCE_DIR}/lib/clock
    ${CMAKE_SOURCE_DIR}/lib/compat
    ${CMAKE_SOURCE_DIR}/lib/config

    ${CMAKE_SOURCE_DIR}/lib/console
    ${CMAKE_SOURCE_DIR}/lib/console/Commands
    ${CMAKE_SOURCE_DIR}/lib/console/cxxopts
    ${CMAKE_SOURCE_DIR}/lib/console/Helpers
    ${CMAKE_SOURCE_DIR}/lib/console/improv
    ${CMAKE_SOURCE_DIR}/lib/console/ute

    ${CMAKE_SOURCE_DIR}/lib/device
    ${CMAKE_SOURCE_DIR}/lib/device/adamnet
    ${CMAKE_SOURCE_DIR}/lib/device/comlynx
    ${CMAKE_SOURCE_DIR}/lib/device/cx16_i2c
    ${CMAKE_SOURCE_DIR}/lib/device/drivewire
    ${CMAKE_SOURCE_DIR}/lib/device/h89
    ${CMAKE_SOURCE_DIR}/lib/device/iec
    ${CMAKE_SOURCE_DIR}/lib/device/iwm
    ${CMAKE_SOURCE_DIR}/lib/device/mac
    ${CMAKE_SOURCE_DIR}/lib/device/rc2014
    ${CMAKE_SOURCE_DIR}/lib/device/rs232
    ${CMAKE_SOURCE_DIR}/lib/device/s100spi
    ${CMAKE_SOURCE_DIR}/lib/device/sio

    ${CMAKE_SOURCE_DIR}/lib/devrelay
    ${CMAKE_SOURCE_DIR}/lib/devrelay/commands
    ${CMAKE_SOURCE_DIR}/lib/devrelay/service
    ${CMAKE_SOURCE_DIR}/lib/devrelay/slip
    ${CMAKE_SOURCE_DIR}/lib/devrelay/types

    ${CMAKE_SOURCE_DIR}/lib/display
    ${CMAKE_SOURCE_DIR}/lib/encoding
    ${CMAKE_SOURCE_DIR}/lib/encrypt
    ${CMAKE_SOURCE_DIR}/lib/FileSystem
    ${CMAKE_SOURCE_DIR}/lib/fn_esp_http_client
    ${CMAKE_SOURCE_DIR}/lib/fnjson
    ${CMAKE_SOURCE_DIR}/lib/ftp
    ${CMAKE_SOURCE_DIR}/lib/fuji
    ${CMAKE_SOURCE_DIR}/lib/hardware
    ${CMAKE_SOURCE_DIR}/lib/http
    ${CMAKE_SOURCE_DIR}/lib/meatloaf
    ${CMAKE_SOURCE_DIR}/lib/media
    ${CMAKE_SOURCE_DIR}/lib/modem-sniffer
    ${CMAKE_SOURCE_DIR}/lib/network-protocol
    ${CMAKE_SOURCE_DIR}/lib/printer-emulator
    ${CMAKE_SOURCE_DIR}/lib/qrcode
    ${CMAKE_SOURCE_DIR}/lib/runcpm
    ${CMAKE_SOURCE_DIR}/lib/sam
    ${CMAKE_SOURCE_DIR}/lib/task
    ${CMAKE_SOURCE_DIR}/lib/tcpip
    ${CMAKE_SOURCE_DIR}/lib/telnet
    ${CMAKE_SOURCE_DIR}/lib/tinyxml2
    ${CMAKE_SOURCE_DIR}/lib/TNFSlib
    ${CMAKE_SOURCE_DIR}/lib/utils
    ${CMAKE_SOURCE_DIR}/lib/webdav
)

FILE(GLOB_RECURSE SOURCES 
    ${CMAKE_SOURCE_DIR}/src/*.cpp

    ${CMAKE_SOURCE_DIR}/lib/bus/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/adamnet/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/comlynx/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/cx16_i2c/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/drivewire/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/h89/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/iec/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/iwm/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/mac/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/rc2014bus/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/rc2014sio/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/rs232/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/s100spi/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/sio/*.cpp

    ${CMAKE_SOURCE_DIR}/lib/clock/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/compat/*.c
    ${CMAKE_SOURCE_DIR}/lib/config/*.cpp

    ${CMAKE_SOURCE_DIR}/lib/console/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/console/Commands/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/console/ute/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/console/improv/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/console/Helpers/*.cpp

    ${CMAKE_SOURCE_DIR}/lib/device/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/adamnet/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/comlynx/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/cx16_i2c/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/drivewire/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/h89/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/iec/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/iwm/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/mac/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/rc2014/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/rs232/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/s100spi/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/sio/*.cpp

    ${CMAKE_SOURCE_DIR}/lib/devrelay/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/devrelay/commands/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/devrelay/service/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/devrelay/slip/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/devrelay/types/*.cpp

    ${CMAKE_SOURCE_DIR}/lib/display/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/encoding/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/encrypt/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/FileSystem/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/fn_esp_http_client/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/fnjson/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/ftp/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/fuji/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/gpiox/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/hardware/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/http/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/meatloaf/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/media/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/media/**/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/modem-sniffer/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/network-protocol/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/printer-emulator/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/qrcode/*.c
    ${CMAKE_SOURCE_DIR}/lib/qrcode/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/sam/*.c
    ${CMAKE_SOURCE_DIR}/lib/sam/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/task/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/tcpip/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/telnet/*.c
    ${CMAKE_SOURCE_DIR}/lib/tinyxml2/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/TNFSlib/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/utils/*.cpp
    ${CMAKE_SOURCE_DIR}/lib/webdav/*.cpp
)

idf_component_register(
    INCLUDE_DIRS ${INCLUDES}
    SRCS ${SOURCES}
    PRIV_REQUIRES esp_driver_uart esp_netif esp_driver_gpio fatfs vfs json esp_wifi mbedtls console app_update spi_flash mlff esp_driver_ledc expat http_parser esp-tls tcp_transport esp_driver_gptimer esp_driver_tsens esp_http_client libssh
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-value)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-but-set-variable)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-parameter)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-const-variable)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-return-type)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-extra-args)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-implicit-fallthrough)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
