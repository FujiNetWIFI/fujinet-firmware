# Gumbo — pure-C HTML5 parser (Codeberg fork, robust on malformed HTML).
# Backend for the gumbo-query component used by lib/fnsgml (FNSGML).
file(GLOB GUMBO_SRCS ${COMPONENT_DIR}/*.c)

idf_component_register(
    SRCS ${GUMBO_SRCS}
    INCLUDE_DIRS "."
)

# Vendored third-party; silence its warnings.
target_compile_options(${COMPONENT_LIB} PRIVATE -w)
