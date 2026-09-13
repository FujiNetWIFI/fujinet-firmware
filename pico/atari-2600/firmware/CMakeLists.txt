cmake_minimum_required(VERSION 3.13)

# Board selection: -DPICO_BOARD=x pulls in boards/x.cmake (feature options,
# platform, flash size) and the SDK finds boards/x.h through
# PICO_BOARD_HEADER_DIRS.
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
if(NOT PICO_BOARD)
    set(PICO_BOARD fujivcs)
endif()
include(${CMAKE_CURRENT_LIST_DIR}/boards/${PICO_BOARD}.cmake)

include(pico_sdk_import.cmake)

project(fujivcs C CXX ASM)

pico_sdk_init()

# include/vcs_font.h is generated, not committed: the glyph table has one
# source (tools/vcsfont.py) and the host tests' reference renderer reads that
# same source, so the C copy cannot drift from what test_render compares
# against. host_test/Makefile builds it too -- but in another CI job, so a
# firmware build that assumes the file is already there only works on a tree
# where the tests have been run.
set(VCS_TOOLS ${CMAKE_CURRENT_LIST_DIR}/../tools)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${VCS_TOOLS}/mkfont.py ${VCS_TOOLS}/vcsfont.py)
execute_process(
    COMMAND python3 ${VCS_TOOLS}/mkfont.py
    OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/vcs_font.h.new
    RESULT_VARIABLE VCS_FONT_RC
)
if(NOT VCS_FONT_RC EQUAL 0)
    message(FATAL_ERROR "mkfont.py failed (${VCS_FONT_RC})")
endif()
# copy_if_different, so a re-configure does not touch the header and force a
# rebuild of everything that includes it.
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_BINARY_DIR}/vcs_font.h.new
    ${CMAKE_CURRENT_LIST_DIR}/include/vcs_font.h
)

add_executable(${PROJECT_NAME}
    src/main.c
    src/vcs_cart.c
    src/fuji_cart.c
    src/vcs_render.c
    src/usb_descriptors.c
)

target_compile_options(${PROJECT_NAME} PRIVATE -Os -Wall)

if(CONFIG_FUJINET)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CONFIG_FUJINET=1)
    target_sources(${PROJECT_NAME} PRIVATE
        src/fujibus.c
        src/fujibus_usb.c
        src/fujimail.c
        src/fujinet.c
        src/fujiboot.c
    )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    pico_stdlib
    pico_multicore
    hardware_vreg
    tinyusb_device
)

pico_add_extra_outputs(${PROJECT_NAME})
