idf_component_register(INCLUDE_DIRS lib SRC_DIRS lib)

target_compile_definitions(${COMPONENT_LIB} PRIVATE PPM_SUPPORTED)
target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_MATH_H)
target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_CTYPE_H)
target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_UNISTD_H)
target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_ERRNO_H)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LZMA_H_INTERNAL)

target_compile_options(${COMPONENT_LIB} PRIVATE  
    -Wno-unused-function
    -Wno-implicit-int
    -Wno-return-type
    -Wno-format-extra-args
    -Wno-unused-parameter
    -Wno-unused-const-variable
    -Wno-implicit-fallthrough
)