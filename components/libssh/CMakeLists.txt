cmake_minimum_required(VERSION 3.12)

# Collect all C sources (recursively) so subdirectories like external/ are
# included. Using a non-recursive glob omitted many .c files and caused
# undefined symbols at link time.
file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)

# Exclude OpenSSL and gcrypt specific files when using mbedTLS
list(FILTER SOURCES EXCLUDE REGEX "/dh_crypto\\.c$")
list(FILTER SOURCES EXCLUDE REGEX "/md_crypto\\.c$")
list(FILTER SOURCES EXCLUDE REGEX "/ecdh_crypto\\.c$")
list(FILTER SOURCES EXCLUDE REGEX "/pki_crypto\\.c$")
# list(FILTER SOURCES EXCLUDE REGEX "/pki_container_openssh\\.c$")  # Enable OpenSSH key support for SFTP
list(FILTER SOURCES EXCLUDE REGEX "/libgcrypt\\.c$")
list(FILTER SOURCES EXCLUDE REGEX "/libcrypto\\.c$")
list(FILTER SOURCES EXCLUDE REGEX "/libcrypto-compat\\.c$")
list(FILTER SOURCES EXCLUDE REGEX "/pki_gcrypt\\.c$")
list(FILTER SOURCES EXCLUDE REGEX "/ecdh_gcrypt\\.c$")
list(FILTER SOURCES EXCLUDE REGEX "/md_gcrypt\\.c$")
list(FILTER SOURCES EXCLUDE REGEX "/getrandom_gcrypt\\.c$")
list(FILTER SOURCES EXCLUDE REGEX "/getrandom_crypto\\.c$")
list(FILTER SOURCES EXCLUDE REGEX "/gcrypt_missing\\.c$")
# Exclude GSSAPI support (not available on ESP-IDF)
list(FILTER SOURCES EXCLUDE REGEX "/gssapi\\.c$")# Exclude ttyopts (uses termios constants not available on ESP-IDF)
list(FILTER SOURCES EXCLUDE REGEX "/ttyopts\.c$")
idf_component_register(
    INCLUDE_DIRS . include src
    SRCS ${SOURCES}
    PRIV_REQUIRES esp_netif mbedtls zlib
)

# Prefer using a compile definition instead of raw -D flags.
target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_CONFIG_H)
# gcm_alt.h checks #if defined(MBEDTLS_GCM_ALT) before esp_config.h gets a
# chance to define it (crypto.h -> gcm_alt.h -> esp_aes_gcm.h -> cipher.h ->
# build_info.h -> esp_config.h), and it's include-guarded, so the macro must
# already be set on the command line or the alt struct/macros never appear.
# Define it with an explicitly empty value (matching esp_config.h's
# `#define MBEDTLS_GCM_ALT`) instead of CMake's implicit "=1", so the two
# definitions are token-identical and GCC doesn't warn about redefinition.
target_compile_definitions(${COMPONENT_LIB} PRIVATE "MBEDTLS_GCM_ALT=")

target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-value
    -Wno-missing-field-initializers
    -Wno-deprecated-declarations
)