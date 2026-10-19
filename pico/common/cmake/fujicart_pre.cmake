# fujicart_pre.cmake -- what a cart's CMakeLists.txt does before project(): set
# FUJI_DEFAULT_BOARD, then include this.
#
# -DPICO_BOARD=x pulls in boards/x.cmake (platform, flash size, feature options) and the SDK
# finds boards/x.h through PICO_BOARD_HEADER_DIRS. FUJI_GEN is where build-cart.sh writes
# fresh generated headers; carts that bake them put it ahead of baked/ on the include path.

set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/boards)
if(NOT PICO_BOARD)
    set(PICO_BOARD ${FUJI_DEFAULT_BOARD})
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/boards/${PICO_BOARD}.cmake)
if(NOT FUJI_GEN)
    set(FUJI_GEN ${CMAKE_CURRENT_SOURCE_DIR}/../build/gen)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)
