cmake_minimum_required(VERSION 3.12)

file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lib/*.c)

idf_component_register( 
  INCLUDE_DIRS . lib
  SRCS ${SOURCES}
  REQUIRES bzip2 expat liblzma lz4 lzma zlib zstd
)

target_compile_options(${COMPONENT_LIB} PRIVATE -DHAVE_CONFIG_H)