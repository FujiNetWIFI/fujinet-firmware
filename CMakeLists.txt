# ESP build is controlled via platformio.ini file
# FUJINET_TARGET variable is used to enforce FujiNet-PC build for specified target

if (NOT "$ENV{FUJINET_TARGET}" STREQUAL "")
    set(FUJINET_TARGET "$ENV{FUJINET_TARGET}" CACHE INTERNAL "FUJINET_TARGET - Set to enforce FujiNet-PC build, set to one of 'ATARI' or 'APPLE', copied from environment variable")
endif()

if (NOT FUJINET_TARGET)
    #### FujiNet ESP ####
    # NOTE: YOU ARE NOT EXPECTED TO CALL THIS FOR ESP TARGET DIRECTLY FROM cmake,
    # USE pio AS NORMAL (e.g. via "build.sh -b") WHICH WILL CALL THIS ITSELF
    # WITH CORRECT ENVIRONMENT

    message(STATUS "Running CMake to build FujiNet for ESP")
    cmake_minimum_required(VERSION 3.16.0)
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    #list(APPEND EXTRA_COMPONENT_DIRS components${IDF_VERSION_MAJOR})
    project(FujiNet_idf)

else()
    #### FujiNet-PC ####

    # To build, e.g. for ATARI target
    #   cd build
    #   cmake .. -DFUJINET_TARGET=ATARI -DCMAKE_BUILD_TYPE=Debug
    #   cmake --build . --target=dist
    # To run
    #   (cd dist && ./run-fujinet)

    # To build - alternative way, using environment variable
    #   cd build
    #   export FUJINET_TARGET=ATARI
    #   cmake -S .. -B $FUJINET_TARGET -DCMAKE_BUILD_TYPE=Debug
    #   cmake --build $FUJINET_TARGET --target=dist
    # To run
    #   (cd $FUJINET_TARGET/dist && ./run-fujinet)

    message(STATUS "Running CMake to build FujiNet-PC (Linux, macOS, Windows), target ${FUJINET_TARGET}")
    include(fujinet_pc.cmake)

endif()
