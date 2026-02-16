idf_component_register(
    SRCS "mongoose.c"
    INCLUDE_DIRS .
    PRIV_REQUIRES mbedtls esp_timer esp_partition app_update
)

# Configure Mongoose for ESP32
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    MG_ARCH=MG_ARCH_ESP32
    MG_ENABLE_MBEDTLS=1
    MG_ENABLE_POSIX_FS=1
    MG_TLS=MG_TLS_MBED
)

# Suppress warnings from mongoose library
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-missing-field-initializers
)
