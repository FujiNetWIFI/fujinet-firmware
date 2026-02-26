# ESP-IDF component for libnfs

# Collect all source files
set(LIBNFS_SOURCES
    # esp32 compatibility layer
    esp32/esp32_compat.c
    # lib sources
    lib/init.c
    lib/krb5-wrapper.c
    lib/libnfs.c
    lib/libnfs-sync.c
    lib/libnfs-zdr.c
    lib/multithreading.c
    lib/nfs_v3.c
    lib/nfs_v4.c
    lib/pdu.c
    lib/socket.c
    # mount sources
    mount/libnfs-raw-mount.c
    mount/mount.c
    # nfs sources
    nfs/libnfs-raw-nfs.c
    nfs/nfs.c
    nfs/nfsacl.c
    # nfs4 sources
    nfs4/libnfs-raw-nfs4.c
    nfs4/nfs4.c
    # nlm sources
    nlm/libnfs-raw-nlm.c
    nlm/nlm.c
    # nsm sources
    nsm/libnfs-raw-nsm.c
    nsm/nsm.c
    # portmap sources
    portmap/libnfs-raw-portmap.c
    portmap/portmap.c
    # rquota sources
    rquota/libnfs-raw-rquota.c
    rquota/rquota.c
)

# Register ESP-IDF component
idf_component_register(
    SRCS ${LIBNFS_SOURCES}
    INCLUDE_DIRS "include" "include/nfsc" "." "esp32"
    PRIV_INCLUDE_DIRS "include" "mount" "nfs" "nfs4" "nlm" "nsm" "portmap" "rquota"
    REQUIRES lwip freertos
)

# Add compile definitions for ESP32
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    HAVE_CONFIG_H
    _U_=
    ESP_PLATFORM=1
)

# Add compile options to suppress warnings - use BEFORE to override global flags
target_compile_options(${COMPONENT_LIB} BEFORE PRIVATE
    -Wno-error=all
    -Wno-error=incompatible-pointer-types
)

# Add additional compile options to suppress specific warnings
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-sign-compare
    -Wno-implicit-fallthrough
    -Wno-type-limits
    -Wno-maybe-uninitialized
    -Wno-implicit-function-declaration
    -Wno-char-subscripts
    -Wno-unused-parameter
    -Wno-incompatible-pointer-types
)

