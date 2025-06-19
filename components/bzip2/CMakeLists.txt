cmake_minimum_required(VERSION 3.12)

file(GLOB SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lib/*.c)

idf_component_register( 
  INCLUDE_DIRS lib
  SRCS ${SOURCES}
)

target_compile_options(${COMPONENT_LIB} PRIVATE  -Wno-unused-function)
target_compile_options(${COMPONENT_LIB} PRIVATE  -Wno-implicit-int)
target_compile_options(${COMPONENT_LIB} PRIVATE  -Wno-return-type)
target_compile_options(${COMPONENT_LIB} PRIVATE  -Wno-format-extra-args)
target_compile_options(${COMPONENT_LIB} PRIVATE  -Wno-unused-parameter)
target_compile_options(${COMPONENT_LIB} PRIVATE  -Wno-unused-const-variable)
target_compile_options(${COMPONENT_LIB} PRIVATE  -Wno-implicit-fallthrough)