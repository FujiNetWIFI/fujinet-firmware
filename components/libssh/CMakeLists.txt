cmake_minimum_required(VERSION 3.12)

file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)

idf_component_register(
    INCLUDE_DIRS include src
    SRCS ${SOURCES}
    PRIV_REQUIRES esp_netif mbedtls
)
