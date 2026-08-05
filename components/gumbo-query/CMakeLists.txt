# gumbo-query — CSS selector engine over Gumbo (patched exception-free and to
# route the DOM to PSRAM; see the FujiNet: comments in the sources). Used by
# lib/fnsgml (FNSGML).
file(GLOB GUMBO_QUERY_SRCS ${COMPONENT_DIR}/*.cpp)

idf_component_register(
    SRCS ${GUMBO_QUERY_SRCS}
    INCLUDE_DIRS "."
    REQUIRES gumbo
)

# Vendored third-party; silence its warnings.
target_compile_options(${COMPONENT_LIB} PRIVATE -w)
