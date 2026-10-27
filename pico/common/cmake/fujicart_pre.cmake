# fujicart_pre.cmake -- a cart's CMakeLists.txt sets FUJI_DEFAULT_BOARD and includes this
# before project(). -DPICO_BOARD=x pulls in boards/x.cmake and boards/x.h. FUJI_GEN is where
# build-cart.sh writes generated headers.

set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/boards)
if(NOT PICO_BOARD)
    set(PICO_BOARD ${FUJI_DEFAULT_BOARD})
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/boards/${PICO_BOARD}.cmake)
if(NOT FUJI_GEN)
    set(FUJI_GEN ${CMAKE_CURRENT_SOURCE_DIR}/../build/gen)
endif()

get_filename_component(FUJI_COMMON ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)

include(${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/fujicart.cmake)
