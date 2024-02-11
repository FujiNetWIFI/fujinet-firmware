# FujiNet-PC

#cmake_minimum_required(VERSION 3.7.2...3.22)
cmake_minimum_required(VERSION 3.4...3.22)
project(fujinet-pc)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(FUJINET_TARGET STREQUAL "ATARI")
    # fujinet.build_platform
    set(FUJINET_BUILD_PLATFORM BUILD_ATARI)
    # fujinet.build_board (used by build_webui.py)
    set(FUJINET_BUILD_BOARD fujinet-pc-atari)
    # fujinet.build_bus
    set(FUJINET_BUILD_BUS SIO)
elseif(FUJINET_TARGET STREQUAL "APPLE")
    # fujinet.build_platform
    set(FUJINET_BUILD_PLATFORM BUILD_APPLE)
    # fujinet.build_board (used by build_webui.py)
    set(FUJINET_BUILD_BOARD fujinet-pc-apple)
    # fujinet.build_bus
    set(FUJINET_BUILD_BUS IWM)
else()
    message(FATAL_ERROR "Invalid target '${FUJINET_TARGET}'! Please choose from 'ATARI' or 'APPLE'.")
endif()

set(SLIP_PROTOCOL "NET" CACHE STRING "Select the protocol type (NET or COM)")

set_property(CACHE SLIP_PROTOCOL PROPERTY STRINGS "NET" "COM")

if(NOT SLIP_PROTOCOL STREQUAL "NET" AND NOT SLIP_PROTOCOL STREQUAL "COM")
  message(FATAL_ERROR "Invalid value for SLIP_PROTOCOL: ${SLIP_PROTOCOL}. Please choose either NET or COM.")
endif()

message(STATUS "SLIP_PROTOCOL is ${SLIP_PROTOCOL}")

# platformio.data_dir (not used by FujiNet-PC)
#set(PLATFORM_DATA_DIR ${CMAKE_SOURCE_DIR}/data/${FUJINET_BUILD_PLATFORM})
# build output data directory (used by build_webui.py)
set(BUILD_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)
# # ESP32 PIN map (not used by FujiNet-PC)
# set(FUJINET_PIN_MAP PINMAP_NONE)

# -DDBUG2 to enable monitor messages for a release build
# -DSKIP_SERVER_CERT_VERIFY does not work with MbedTLS
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D${FUJINET_BUILD_PLATFORM} -DSP_OVER_SLIP -DFLASH_SPIFFS -DDBUG2")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DVERBOSE_HTTP -D__PC_BUILD_DEBUG__")

# mongoose.c some compile options: -DMG_ENABLE_LINES=1 -DMG_ENABLE_DIRECTORY_LISTING=1 -DMG_ENABLE_SSI=1
# # use OpenSSL
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D${FUJINET_BUILD_PLATFORM} -DMG_ENABLE_OPENSSL=1 -DMG_ENABLE_LOG=0")
# use MbedTLS
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D${FUJINET_BUILD_PLATFORM} -DMG_ENABLE_MBEDTLS=1 -DMG_ENABLE_LOG=0 -DSP_OVER_SLIP")

# INCLUDE (CheckIncludeFiles)
# CHECK_INCLUDE_FILES (bsd/string.h HAVE_BSD_STRING_H)
# CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/include/config.h)
# set(INCLUDE_DIRS include ${CMAKE_CURRENT_BINARY_DIR}/include

set(INCLUDE_DIRS include
    lib/compat lib/config lib/utils lib/hardware
    lib/FileSystem lib/EdUrlParser
    lib/tcpip lib/ftp lib/TNFSlib lib/telnet lib/fnjson
    lib/webdav lib/http lib/sam lib/task
    lib/modem-sniffer lib/printer-emulator
    lib/network-protocol 
    lib/fuji lib/bus lib/device lib/media
    lib/encrypt lib/base64
    lib/slip
    components_pc/mongoose
    components_pc/cJSON
    components_pc/libsmb2/include
    components_pc/libssh/include ${CMAKE_CURRENT_BINARY_DIR}/components_pc/libssh/include
)

set(SOURCES src/main.cpp
    lib/config/fnConfig.h lib/config/fnConfig.cpp
    lib/config/fnc_bt.cpp
    lib/config/fnc_cassette.cpp
    lib/config/fnc_cpm.cpp
    lib/config/fnc_enable.cpp
    lib/config/fnc_general.cpp
    lib/config/fnc_hosts.cpp
    lib/config/fnc_load.cpp
    lib/config/fnc_modem.cpp
    lib/config/fnc_mounts.cpp
    lib/config/fnc_network.cpp
    lib/config/fnc_phonebook.cpp
    lib/config/fnc_printer.cpp
    lib/config/fnc_save.cpp
    lib/config/fnc_serial.cpp
    lib/config/fnc_util.cpp
    lib/config/fnc_wifi.cpp
    include/debug.h 
    lib/utils/utils.h lib/utils/utils.cpp
    lib/utils/cbuf.h lib/utils/cbuf.cpp
    lib/utils/string_utils.h lib/utils/string_utils.cpp
    lib/utils/peoples_url_parser.h lib/utils/peoples_url_parser.cpp
    lib/utils/punycode.h lib/utils/punycode.cpp
    lib/utils/U8Char.h lib/utils/U8Char.cpp
    lib/hardware/fnWiFi.h lib/hardware/fnDummyWiFi.h lib/hardware/fnDummyWiFi.cpp
    lib/hardware/led.h lib/hardware/led.cpp
    lib/hardware/fnUART.h lib/hardware/fnUART.cpp 
    lib/hardware/fnUARTUnix.cpp lib/hardware/fnUARTWindows.cpp
    lib/hardware/fnSystem.h lib/hardware/fnSystem.cpp lib/hardware/fnSystemNet.cpp
    lib/FileSystem/fnDirCache.h lib/FileSystem/fnDirCache.cpp
    lib/FileSystem/fnFS.h lib/FileSystem/fnFS.cpp
    lib/FileSystem/fnFsSPIFFS.h lib/FileSystem/fnFsSPIFFS.cpp
    lib/FileSystem/fnFsSD.h lib/FileSystem/fnFsSD.cpp
    lib/FileSystem/fnFsTNFS.h lib/FileSystem/fnFsTNFS.cpp
    lib/FileSystem/fnFsSMB.h lib/FileSystem/fnFsSMB.cpp
    lib/FileSystem/fnFsFTP.h lib/FileSystem/fnFsFTP.cpp
    lib/FileSystem/fnFile.h lib/FileSystem/fnFile.cpp
    lib/FileSystem/fnFileLocal.h lib/FileSystem/fnFileLocal.cpp
    lib/FileSystem/fnFileTNFS.h lib/FileSystem/fnFileTNFS.cpp
    lib/FileSystem/fnFileSMB.h lib/FileSystem/fnFileSMB.cpp
    lib/FileSystem/fnFileMem.h lib/FileSystem/fnFileMem.cpp
    
    lib/tcpip/fnDNS.h lib/tcpip/fnDNS.cpp
    lib/tcpip/fnUDP.h lib/tcpip/fnUDP.cpp
    lib/tcpip/fnTcpClient.h lib/tcpip/fnTcpClient.cpp
    lib/tcpip/fnTcpServer.h lib/tcpip/fnTcpServer.cpp
    lib/ftp/fnFTP.h lib/ftp/fnFTP.cpp
    lib/TNFSlib/tnfslibMountInfo.h lib/TNFSlib/tnfslibMountInfo.cpp
    lib/TNFSlib/tnfslib.h lib/TNFSlib/tnfslib.cpp
    lib/telnet/libtelnet.h lib/telnet/libtelnet.c
    lib/fnjson/fnjson.h lib/fnjson/fnjson.cpp
    components_pc/mongoose/mongoose.h components_pc/mongoose/mongoose.c
    lib/webdav/WebDAV.h lib/webdav/WebDAV.cpp
    lib/http/httpService.h lib/http/mgHttpService.cpp
    lib/http/httpServiceParser.h lib/http/httpServiceParser.cpp
    lib/http/httpServiceConfigurator.h lib/http/httpServiceConfigurator.cpp
    lib/http/httpServiceBrowser.h lib/http/httpServiceBrowser.cpp
    lib/http/mgHttpClient.h lib/http/mgHttpClient.cpp
    lib/task/fnTask.h lib/task/fnTask.cpp
    lib/task/fnTaskManager.h lib/task/fnTaskManager.cpp
    lib/printer-emulator/atari_1020.h lib/printer-emulator/atari_1020.cpp
    lib/printer-emulator/atari_1025.h lib/printer-emulator/atari_1025.cpp
    lib/printer-emulator/atari_1027.h lib/printer-emulator/atari_1027.cpp
    lib/printer-emulator/atari_1029.h lib/printer-emulator/atari_1029.cpp
    lib/printer-emulator/atari_820.h lib/printer-emulator/atari_820.cpp
    lib/printer-emulator/atari_822.h lib/printer-emulator/atari_822.cpp
    lib/printer-emulator/atari_825.h lib/printer-emulator/atari_825.cpp
    lib/printer-emulator/atari_xdm121.h lib/printer-emulator/atari_xdm121.cpp
    lib/printer-emulator/atari_xmm801.h lib/printer-emulator/atari_xmm801.cpp
    lib/printer-emulator/epson_80.h lib/printer-emulator/epson_80.cpp
    lib/printer-emulator/epson_tps.h
    lib/printer-emulator/file_printer.h lib/printer-emulator/file_printer.cpp
    lib/printer-emulator/html_printer.h lib/printer-emulator/html_printer.cpp
    lib/printer-emulator/okimate_10.h lib/printer-emulator/okimate_10.cpp
    lib/printer-emulator/pdf_printer.h lib/printer-emulator/pdf_printer.cpp
    lib/printer-emulator/png_printer.h lib/printer-emulator/png_printer.cpp
    lib/printer-emulator/printer_emulator.h lib/printer-emulator/printer_emulator.cpp
    lib/printer-emulator/svg_plotter.h lib/printer-emulator/svg_plotter.cpp
    lib/network-protocol/networkStatus.h lib/network-protocol/status_error_codes.h
    lib/network-protocol/Protocol.h lib/network-protocol/Protocol.cpp
    lib/network-protocol/ProtocolParser.h lib/network-protocol/ProtocolParser.cpp
    lib/network-protocol/Test.h lib/network-protocol/Test.cpp
    lib/network-protocol/TCP.h lib/network-protocol/TCP.cpp
    lib/network-protocol/UDP.h lib/network-protocol/UDP.cpp
    lib/network-protocol/Telnet.h lib/network-protocol/Telnet.cpp
    lib/network-protocol/FS.h lib/network-protocol/FS.cpp
    lib/network-protocol/FTP.h lib/network-protocol/FTP.cpp
    lib/network-protocol/TNFS.h lib/network-protocol/TNFS.cpp
    lib/network-protocol/HTTP.h lib/network-protocol/HTTP.cpp
    lib/network-protocol/SMB.h lib/network-protocol/SMB.cpp
    lib/network-protocol/SSH.h lib/network-protocol/SSH.cpp
    lib/network-protocol/SD.h lib/network-protocol/SD.cpp
    lib/fuji/fujiCmd.h
    lib/fuji/fujiHost.h lib/fuji/fujiHost.cpp
    lib/fuji/fujiDisk.h lib/fuji/fujiDisk.cpp
    lib/bus/bus.h
    lib/device/device.h
    lib/device/disk.h
    lib/device/printer.h
    lib/device/modem.h
    lib/device/cassette.h
    lib/device/fuji.h
    lib/device/network.h
    lib/device/udpstream.h
    lib/device/siocpm.h
    lib/modem/modem.h lib/modem/modem.cpp
    lib/modem-sniffer/modem-sniffer.h lib/modem-sniffer/modem-sniffer.cpp
    lib/media/media.h
    lib/encoding/base64.h lib/encoding/base64.cpp
    lib/encoding/hash.h lib/encoding/hash.cpp
    lib/encrypt/crypt.h lib/encrypt/crypt.cpp
    lib/compat/compat_inet.c
    lib/compat/compat_gettimeofday.h lib/compat/compat_gettimeofday.c
)

if(FUJINET_TARGET STREQUAL "ATARI")
    list(APPEND SOURCES

    lib/bus/sio/sio.h lib/bus/sio/sio.cpp
    lib/bus/sio/siocom/sioport.h lib/bus/sio/siocom/sioport.cpp
    lib/bus/sio/siocom/serialsio.h lib/bus/sio/siocom/serialsio.cpp
    lib/bus/sio/siocom/netsio.h lib/bus/sio/siocom/netsio.cpp
    lib/bus/sio/siocom/fnSioCom.h lib/bus/sio/siocom/fnSioCom.cpp
    lib/media/atari/diskType.h lib/media/atari/diskType.cpp
    lib/media/atari/diskTypeAtr.h lib/media/atari/diskTypeAtr.cpp
    lib/media/atari/diskTypeAtx.h 
    lib/media/atari/diskTypeXex.h lib/media/atari/diskTypeXex.cpp

    lib/device/sio/disk.h lib/device/sio/disk.cpp
    lib/device/sio/printer.h lib/device/sio/printer.cpp
    lib/device/sio/printerlist.h lib/device/sio/printerlist.cpp
    lib/device/sio/cassette.h lib/device/sio/cassette.cpp
    lib/device/sio/fuji.h lib/device/sio/fuji.cpp
    lib/device/sio/network.h lib/device/sio/network.cpp
    lib/device/sio/udpstream.h lib/device/sio/udpstream.cpp
    #lib/device/sio/voice.h lib/device/sio/voice.cpp
    lib/device/sio/apetime.h lib/device/sio/apetime.cpp
    lib/device/sio/siocpm.h lib/device/sio/siocpm.cpp
    lib/device/sio/pclink.h lib/device/sio/pclink.cpp

    )
endif()

if(FUJINET_TARGET STREQUAL "APPLE")
    list(APPEND SOURCES

    lib/bus/iwm/iwm.h lib/bus/iwm/iwm.cpp
    lib/slip/SPoSLIP.h
    lib/slip/Packet.h
    lib/slip/SmartPortCodes.h
    lib/slip/Response.h lib/slip/Response.cpp
    lib/slip/Request.h lib/slip/Request.cpp
    lib/slip/SLIP.h lib/slip/SLIP.cpp
    lib/slip/CloseRequest.h lib/slip/CloseRequest.cpp
    lib/slip/CloseResponse.h lib/slip/CloseResponse.cpp
    lib/slip/ControlRequest.h lib/slip/ControlRequest.cpp
    lib/slip/ControlResponse.h lib/slip/ControlResponse.cpp
    lib/slip/FormatRequest.h lib/slip/FormatRequest.cpp
    lib/slip/FormatResponse.h lib/slip/FormatResponse.cpp
    lib/slip/InitRequest.h lib/slip/InitRequest.cpp
    lib/slip/InitResponse.h lib/slip/InitResponse.cpp
    lib/slip/OpenRequest.h lib/slip/OpenRequest.cpp
    lib/slip/OpenResponse.h lib/slip/OpenResponse.cpp
    lib/slip/ReadBlockRequest.h lib/slip/ReadBlockRequest.cpp
    lib/slip/ReadBlockResponse.h lib/slip/ReadBlockResponse.cpp
    lib/slip/ReadRequest.h lib/slip/ReadRequest.cpp
    lib/slip/ReadResponse.h lib/slip/ReadResponse.cpp
    lib/slip/ResetRequest.h lib/slip/ResetRequest.cpp
    lib/slip/ResetResponse.h lib/slip/ResetResponse.cpp
    lib/slip/StatusRequest.h lib/slip/StatusRequest.cpp
    lib/slip/StatusResponse.h lib/slip/StatusResponse.cpp
    lib/slip/WriteBlockRequest.h lib/slip/WriteBlockRequest.cpp
    lib/slip/WriteBlockResponse.h lib/slip/WriteBlockResponse.cpp
    lib/slip/WriteRequest.h lib/slip/WriteRequest.cpp
    lib/slip/WriteResponse.h lib/slip/WriteResponse.cpp

    lib/media/apple/mediaType.h lib/media/apple/mediaType.cpp
    lib/media/apple/mediaTypeDO.h lib/media/apple/mediaTypeDO.cpp
    lib/media/apple/mediaTypeDSK.h lib/media/apple/mediaTypeDSK.cpp
    lib/media/apple/mediaTypePO.h lib/media/apple/mediaTypePO.cpp
    lib/media/apple/mediaTypeWOZ.h lib/media/apple/mediaTypeWOZ.cpp

    lib/device/iwm/disk.h lib/device/iwm/disk.cpp
    lib/device/iwm/disk2.h lib/device/iwm/disk2.cpp
    lib/device/iwm/printer.h lib/device/iwm/printer.cpp
    lib/device/iwm/printerlist.h lib/device/iwm/printerlist.cpp
    lib/device/iwm/modem.h lib/device/iwm/modem.cpp
    lib/device/iwm/fuji.h lib/device/iwm/fuji.cpp
    lib/device/iwm/network.h lib/device/iwm/network.cpp
    lib/device/iwm/clock.h lib/device/iwm/clock.cpp
    lib/device/iwm/cpm.h lib/device/iwm/cpm.cpp

    )
endif()

if(SLIP_PROTOCOL STREQUAL "NET")
    list(APPEND SOURCES
        lib/bus/iwm/iwm_slip.h lib/utils/std_extensions.hpp lib/bus/iwm/iwm_slip.cpp
        lib/bus/iwm/Connection.h lib/bus/iwm/Connection.cpp
        lib/bus/iwm/TCPConnection.h lib/bus/iwm/TCPConnection.cpp
    )
elseif(SLIP_PROTOCOL STREQUAL "COM")
    # Append sources specific to COM
    list(APPEND SOURCES
    )
endif()


if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(SOURCES ${SOURCES} lib/compat/win32_uname.c)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # strlcpy and strlcat are part of stdlib on mac
else()
    # compile strlcpy and strlcat (from OpenBSD)
    set(SOURCES ${SOURCES} lib/compat/strlcat.c lib/compat/strlcpy.c)
endif()

add_executable(fujinet ${SOURCES})

# Libraries
# build and link static libs
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# # OpenSSL
# if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
#     set(OPENSSL_ROOT_DIR /usr/local/opt/openssl)
#     set(OPENSSL_USE_STATIC_LIBS TRUE) # set this before adding libsmb2
# endif()
# find_package(OpenSSL REQUIRED)
# set(CRYPTO_LIBS OpenSSL::SSL OpenSSL::Crypto)
# target_include_directories(fujinet PRIVATE ${INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR})

# Mbed TLS
# https://github.com/Mbed-TLS/mbedtls
# - to build from source (failed on Windows/MSYS2)
# add_subdirectory(components_pc/mbedtls)
# - to use library package (Ubuntu deb package is old, does not support cmake/find_package)
# find_package(MbedTLS)
# - try to find necessary files in system ...
find_library(MBEDTLS_STATIC_LIB libmbedtls.a /usr/lib /usr/local/lib /usr/local/opt /clang64/lib /clang32/lib)
find_library(MBEDX509_STATIC_LIB libmbedx509.a /usr/lib /usr/local/lib /usr/local/opt /clang64/lib /clang32/lib)
find_library(MBEDCRYPTO_STATIC_LIB libmbedcrypto.a /usr/lib /usr/local/lib /usr/local/opt /clang64/lib /clang32/lib)
find_path(MBEDTLS_INCLUDE_DIR mbedtls/ssl.h /usr/include /usr/local/include /usr/local/lib /usr/local/opt /clang64/include /clang32/include)
set(CRYPTO_LIBS ${MBEDTLS_STATIC_LIB} ${MBEDX509_STATIC_LIB} ${MBEDCRYPTO_STATIC_LIB})
# message("MBEDTLS_STATIC_LIB=${MBEDTLS_STATIC_LIB}")
# message("MBEDX509_STATIC_LIB=${MBEDX509_STATIC_LIB}")
# message("MBEDCRYPTO_STATIC_LIB=${MBEDCRYPTO_STATIC_LIB}")
# message("MBEDTLS_INCLUDE_DIR=${MBEDTLS_INCLUDE_DIR}")
target_include_directories(fujinet PRIVATE ${INCLUDE_DIRS} ${MBEDTLS_INCLUDE_DIR})

# cJSON library
# https://github.com/DaveGamble/cJSON
set(ENABLE_CJSON_UTILS ON CACHE BOOL "Enable building the cJSON_Utils library.")
set(ENABLE_CJSON_TEST OFF CACHE BOOL "Enable building cJSON test")
# set(CJSON_OVERRIDE_BUILD_SHARED_LIBS ON CACHE BOOL "Override BUILD_SHARED_LIBS with CJSON_BUILD_SHARED_LIBS")
# set(CJSON_BUILD_SHARED_LIBS OFF CACHE BOOL "Overrides BUILD_SHARED_LIBS if CJSON_OVERRIDE_BUILD_SHARED_LIBS is enabled")
add_subdirectory(components_pc/cJSON)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    #add_link_options("-fstack-protector") # requires newer cmake
    set(CMAKE_EXE_LINKER_FLAGS "-fstack-protector") # works with old cmake
    #target_link_libraries(fujinet ssp) # it seems above linker option is not enough
endif()

# Libsmb2
# https://github.com/sahlberg/libsmb2
add_subdirectory(components_pc/libsmb2)

# libssh
# https://www.libssh.org/
# - FujiNet (platfomio/ESP32) port
# add_subdirectory(lib/libssh)
# - Regular elease
add_subdirectory(components_pc/libssh)

target_link_libraries(fujinet pthread expat cjson cjson_utils smb2 ssh)
target_link_libraries(fujinet ${CRYPTO_LIBS})

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(fujinet ws2_32 bcrypt)
endif()

# TODO megre build_version.py with ESP version
# # Version file

# # run build_version.py to update version.h
# add_custom_command(
#   OUTPUT  "${CMAKE_BINARY_DIR}/version.h"
#   DEPENDS build_version.py
#   WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
#   COMMAND python build_version.py
#   COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/include/version.h" "${CMAKE_BINARY_DIR}/version.h"
#   COMMENT "Update version file"
#   VERBATIM
# )
# add_custom_target(build_version DEPENDS "${CMAKE_BINARY_DIR}/version.h")
# add_dependencies(fujinet build_version)

# WebUI
# "build_webui" target
add_custom_command(
    OUTPUT "${BUILD_DATA_DIR}"
    DEPENDS build_webui.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND ${CMAKE_COMMAND} -E env
      FUJINET_BUILD_BOARD=${FUJINET_BUILD_BOARD}
      FUJINET_BUILD_PLATFORM=${FUJINET_BUILD_PLATFORM}
      BUILD_DATA_DIR=${BUILD_DATA_DIR}
      python3 build_webui.py
)
add_custom_target(build_webui DEPENDS "${BUILD_DATA_DIR}")

# "dist" target
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_custom_target(dist
        COMMENT "Preparing dist directory"
        COMMAND ${CMAKE_COMMAND} -E make_directory dist
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/distfiles dist
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:fujinet> dist
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${BUILD_DATA_DIR} dist/data
        # DLL's TODO how to make this using cmake?
        COMMAND ldd $<TARGET_FILE:fujinet> | grep -v -i '/windows' 
        | awk '{print $$3}' | xargs -I {} cp -p {} dist
    )
else()
    add_custom_target(dist
        COMMENT "Preparing dist directory"
        COMMAND ${CMAKE_COMMAND} -E make_directory dist
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/distfiles dist
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:fujinet> dist
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${BUILD_DATA_DIR} dist/data
        COMMAND ${CMAKE_COMMAND} -E remove dist/run-fujinet.bat
        COMMAND ${CMAKE_COMMAND} -E remove dist/run-fujinet.ps1
    )
endif()
add_dependencies(dist fujinet)
add_dependencies(dist build_webui)

# include dist cleanup in "clean" target
set_property(
    DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES dist
)
# include data cleanup in "clean" target
set_property(
    DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES ${BUILD_DATA_DIR}
)
