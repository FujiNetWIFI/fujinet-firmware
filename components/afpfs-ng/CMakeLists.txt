file(GLOB_RECURSE SRCS "lib/*.c" "esp32/*.c")

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS . include esp32
                       REQUIRES mbedtls esp_system)

# Force-include the ESP32 compat header into every afpfs-ng source file.
# This gives loop.c the pselect() prototype without modifying library sources.
if(IDF_TARGET)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -include ${CMAKE_CURRENT_SOURCE_DIR}/esp32/esp32_compat.h)
endif()

# Suppress warnings from the upstream library sources (same policy as libnfs).
target_compile_options(${COMPONENT_LIB} BEFORE PRIVATE
    -Wno-error=all
    -Wno-error=incompatible-pointer-types
)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-implicit-function-declaration
    -Wno-incompatible-pointer-types
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-missing-field-initializers
)