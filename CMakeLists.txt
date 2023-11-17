if (NOT "$ENV{FUJINET_PC}" STREQUAL "")
    set(FUJINET_PC "$ENV{FUJINET_PC}" CACHE INTERNAL "FUJINET_PC - Set non-zero to enforce FujiNet-PC build, copied from environment variable")
endif()

if (NOT FUJINET_PC)
    #### FujiNet ESP ####

    message("Running CMake to build FujiNet for ESP")
    cmake_minimum_required(VERSION 3.16.0)
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    list(APPEND EXTRA_COMPONENT_DIRS components${IDF_VERSION_MAJOR})
    project(FujiNet_idf)

else()
    #### FujiNet PC ####

    # Call cmake {SOURCE_DIR} -DFUJINET_PC=1 -DFUJINET_PLATFORM=[ATARI|APPLE] -DCMAKE_BUILD_TYPE=[Debug|RelWitthDebInfo|Release]
    if (NOT "$ENV{FUJINET_PLATFORM}" STREQUAL "")
        set(FUJINET_PLATFORM "$ENV{FUJINET_PLATFORM}" CACHE INTERNAL "FUJINET_PLATFORM - Set to one of ATARI APPLE, copied from environment variable")
    endif()
    if (NOT FUJINET_PLATFORM)
        message(FATAL_ERROR "To build FujiNet-PC set FUJINET_PLATFORM to one of ATARI APPLE")
    endif()
    message("Running CMake to build FujiNet for PC (Linux/macOS/Windows), platform ${FUJINET_PLATFORM}")
    include(fujinet_pc.cmake)

endif()
