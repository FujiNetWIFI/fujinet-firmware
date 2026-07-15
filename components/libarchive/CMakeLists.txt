cmake_minimum_required(VERSION 3.12)

file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lib/*.c)

# ESP32 POSIX stubs for archive_write_disk_posix.c
list(APPEND SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/esp32/esp32_compat.c)

# Add ESP32 HIMEM allocator for LZMA
#list(APPEND SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/esp32/esp32_himem_allocator.c)

idf_component_register(
    INCLUDE_DIRS . lib
    SRCS ${SOURCES}
    REQUIRES bzip2 expat liblzma lz4 lzma zlib zstd esp_psram
)

target_compile_options(${COMPONENT_LIB} PRIVATE -DHAVE_CONFIG_H)
target_compile_options(${COMPONENT_LIB} PRIVATE -DUSE_ESP32_HIMEM=0)

target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wno-unused-function
)
