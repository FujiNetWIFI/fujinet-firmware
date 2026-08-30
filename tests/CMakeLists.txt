cmake_minimum_required(VERSION 3.16)

add_executable(fujibuspacket_tests
    FujiBusPacketTests.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/rs232/FujiBusPacket.cpp
)

target_include_directories(fujibuspacket_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include/      # for debug.h, etc.
    ${CMAKE_SOURCE_DIR}/lib/
    ${CMAKE_SOURCE_DIR}/lib/bus/
    ${CMAKE_SOURCE_DIR}/components_pc/
)

target_link_libraries(fujibuspacket_tests PRIVATE
)

add_test(NAME fujibuspacket_tests COMMAND fujibuspacket_tests)

# ------------------------------------------------------------------------------
# IMD (ImageDisk) core media type. This also serves as the compile check for
# lib/media/IMDImage.cpp on the PC side: no PC target links it yet, since no
# platform adapter exists.
# UNIT_TESTS is required - fujinet_pc.cmake puts -DDBUG2 on CMAKE_CXX_FLAGS,
# which turns on DEBUG, which makes debug.h pull in <utils.h> that we do not
# link here. debug.h undefines DEBUG when UNIT_TESTS is set.
# FNIO_IS_STDIO pins fnFile to std::FILE: we inherit -D${FUJINET_BUILD_PLATFORM}
# from CMAKE_CXX_FLAGS, which would otherwise select FileHandler and drag the
# whole FileSystem stack in. The FileHandler path is covered by the ESP builds,
# which glob lib/media/*.cpp into every target.
# ------------------------------------------------------------------------------
add_executable(imdimage_tests
    IMDImageTests.cpp
    ${CMAKE_SOURCE_DIR}/lib/media/IMDImage.cpp
)

target_compile_definitions(imdimage_tests PRIVATE UNIT_TESTS FNIO_IS_STDIO)

target_include_directories(imdimage_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include/      # debug.h, global_types.h
    ${CMAKE_SOURCE_DIR}/lib/
    ${CMAKE_SOURCE_DIR}/lib/FileSystem/
    ${CMAKE_SOURCE_DIR}/lib/media/
    ${CMAKE_SOURCE_DIR}/components_pc/
)

add_test(NAME imdimage_tests COMMAND imdimage_tests)

# ------------------------------------------------------------------------------
# Policy check: no platform-specific BUILD_* ifdefs in the shared device bases.
# Customization there must go through a subclass override, never a #ifdef.
# This is a script, not a compiled test, so it's wired in directly via
# add_test rather than as a doctest executable.
# ------------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
    NAME no_build_ifdefs_in_fujidevice
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/check_no_build_ifdefs.py
            ${CMAKE_SOURCE_DIR}/lib/device/fujiDevice
            ${CMAKE_SOURCE_DIR}/lib/device/fujiClock
)
