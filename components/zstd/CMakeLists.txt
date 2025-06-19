# ################################################################
# Copyright (c) Meta Platforms, Inc. and affiliates.
# All rights reserved.
#
# This source code is licensed under both the BSD-style license (found in the
# LICENSE file in the root directory of this source tree) and the GPLv2 (found
# in the COPYING file in the root directory of this source tree).
# ################################################################

cmake_minimum_required(VERSION 3.12)

set(LIBRARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)

set(CMAKE_INCLUDE_CURRENT_DIR TRUE)
#option(ZSTD_BUILD_STATIC "BUILD STATIC LIBRARIES" ON)
#option(ZSTD_BUILD_SHARED "BUILD SHARED LIBRARIES" ON)
option(ZSTD_BUILD_COMPRESSION "BUILD COMPRESSION MODULE" OFF)
option(ZSTD_BUILD_DECOMPRESSION "BUILD DECOMPRESSION MODULE" ON)
option(ZSTD_BUILD_DICTBUILDER "BUILD DICTBUILDER MODULE" ON)
option(ZSTD_BUILD_DEPRECATED "BUILD DEPRECATED MODULE" OFF)
option(ZSTD_LEGACY_SUPPORT "BUILD LEGACY SUPPORT" OFF)

set(ZSTDLIB_VISIBLE "" CACHE STRING "Visibility for ZSTDLIB API")
set(ZSTDERRORLIB_VISIBLE "" CACHE STRING "Visibility for ZSTDERRORLIB_VISIBLE API")
set(ZDICTLIB_VISIBLE "" CACHE STRING "Visibility for ZDICTLIB_VISIBLE API")
set(ZSTDLIB_STATIC_API "" CACHE STRING "Visibility for ZSTDLIB_STATIC_API API")
set(ZDICTLIB_STATIC_API "" CACHE STRING "Visibility for ZDICTLIB_STATIC_API API")

set_property(CACHE ZSTDLIB_VISIBLE PROPERTY STRINGS "" "hidden" "default" "protected" "internal")
set_property(CACHE ZSTDERRORLIB_VISIBLE PROPERTY STRINGS "" "hidden" "default" "protected" "internal")
set_property(CACHE ZDICTLIB_VISIBLE PROPERTY STRINGS "" "hidden" "default" "protected" "internal")
set_property(CACHE ZSTDLIB_STATIC_API PROPERTY STRINGS "" "hidden" "default" "protected" "internal")
set_property(CACHE ZDICTLIB_STATIC_API PROPERTY STRINGS "" "hidden" "default" "protected" "internal")

file(GLOB CommonSources ${LIBRARY_DIR}/common/*.c)
set(Sources ${CommonSources})
set(Headers ${LIBRARY_DIR})
if (ZSTD_BUILD_COMPRESSION)
    file(GLOB CompressSources ${LIBRARY_DIR}/compress/*.c)
    set(Sources ${Sources} ${CompressSources})
    set(Headers ${Headers} ${LIBRARY_DIR}/compress)
endif()
if (ZSTD_BUILD_DECOMPRESSION)
    file(GLOB DecompressSources ${LIBRARY_DIR}/decompress/*.c)
    set(Sources ${Sources} ${DecompressSources})
    set(Headers ${Headers} ${LIBRARY_DIR}/decompress)
endif()
if (ZSTD_BUILD_DICTBUILDER)
    file(GLOB DictBuilderSources ${LIBRARY_DIR}/dictBuilder/*.c)
    set(Sources ${Sources} ${DictBuilderSources})
    set(Headers ${Headers} ${LIBRARY_DIR}/dictBuilder)
endif()
if (ZSTD_BUILD_DEPRECATED)
    file(GLOB DeprecatedSources ${LIBRARY_DIR}/deprecated/*.c)
    set(Sources ${Sources} ${DeprecatedSources})
    set(Headers ${Headers} ${LIBRARY_DIR}/deprecated)
endif()

if (ZSTD_LEGACY_SUPPORT)
    set(LIBRARY_LEGACY_DIR ${LIBRARY_DIR}/legacy)
    set(Headers ${Headers} ${LIBRARY_LEGACY_DIR})

    set(Sources ${Sources}
            ${LIBRARY_LEGACY_DIR}/zstd_v01.c
            ${LIBRARY_LEGACY_DIR}/zstd_v02.c
            ${LIBRARY_LEGACY_DIR}/zstd_v03.c
            ${LIBRARY_LEGACY_DIR}/zstd_v04.c
            ${LIBRARY_LEGACY_DIR}/zstd_v05.c
            ${LIBRARY_LEGACY_DIR}/zstd_v06.c
            ${LIBRARY_LEGACY_DIR}/zstd_v07.c)
endif ()


idf_component_register( SRCS ${Sources} INCLUDE_DIRS ${Headers} )

