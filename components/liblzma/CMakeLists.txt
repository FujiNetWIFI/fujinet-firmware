cmake_minimum_required(VERSION 3.12)

file(GLOB Sources ${CMAKE_CURRENT_SOURCE_DIR}/common/*.c)
set(Headers ${CMAKE_CURRENT_SOURCE_DIR}/common)

set(LIBRARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)

set(Headers ${Headers} ${LIBRARY_DIR}/api)
#set(Headers ${Headers} ${LIBRARY_DIR}/api/lzma)

file(GLOB CSources ${LIBRARY_DIR}/check/*.c)
set(Sources ${Sources} ${CSources})
set(Headers ${Headers} ${LIBRARY_DIR}/check)

file(GLOB CSources ${LIBRARY_DIR}/common/*.c)
set(Sources ${Sources} ${CSources})
set(Headers ${Headers} ${LIBRARY_DIR}/common)

file(GLOB CSources ${LIBRARY_DIR}/delta/*.c)
set(Sources ${Sources} ${CSources})
set(Headers ${Headers} ${LIBRARY_DIR}/delta)

file(GLOB CSources ${LIBRARY_DIR}/lz/*.c)
set(Sources ${Sources} ${CSources})
set(Headers ${Headers} ${LIBRARY_DIR}/lz)

file(GLOB CSources ${LIBRARY_DIR}/lzma/*.c)
set(Sources ${Sources} ${CSources})
set(Headers ${Headers} ${LIBRARY_DIR}/lzma)

file(GLOB CSources ${LIBRARY_DIR}/rangecoder/*.c)
set(Sources ${Sources} ${CSources})
set(Headers ${Headers} ${LIBRARY_DIR}/rangecoder)

file(GLOB CSources ${LIBRARY_DIR}/simple/*.c)
set(Sources ${Sources} ${CSources})
set(Headers ${Headers} ${LIBRARY_DIR}/simple)

idf_component_register( SRCS ${Sources} INCLUDE_DIRS . ${Headers} )

target_compile_options(${COMPONENT_LIB} PRIVATE -DHAVE_CONFIG_H)