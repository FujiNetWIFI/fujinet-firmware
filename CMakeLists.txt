if (NOT "$ENV{FUJINET_PC}" STREQUAL "")
    set(FUJINET_PC "$ENV{FUJINET_PC}" CACHE INTERNAL "FUJINET_PC - Set non-zero to enforce FujiNet-PC build, copied from environment variable")
endif()

if (NOT FUJINET_PC)
#### FujiNet ESP ####
message(STATUS "Running CMake to build FujiNet for ESP platform")
cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
list(APPEND EXTRA_COMPONENT_DIRS components${IDF_VERSION_MAJOR})
project(FujiNet_idf)

else()
#### FujiNet PC ####
message(STATUS "Running CMake to build FujiNet for PC platform (Linux/macOS/Windows)")
include(fujinet_pc.cmake)

endif()
