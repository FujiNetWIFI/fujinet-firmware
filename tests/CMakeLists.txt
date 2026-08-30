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
# Calendar date arithmetic. fn_time is deliberately free of libc time functions
# and of any FujiNet dependency, so it links on its own - which is what makes the
# DST and parsing edge cases cheap to pin down here rather than on hardware.
# ------------------------------------------------------------------------------
add_executable(calendar_tests
    CalendarTests.cpp
    ${CMAKE_SOURCE_DIR}/lib/utils/fn_time.cpp
)

target_include_directories(calendar_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include/
    ${CMAKE_SOURCE_DIR}/lib/
    ${CMAKE_SOURCE_DIR}/components_pc/   # doctest
)

add_test(NAME calendar_tests COMMAND calendar_tests)

# ------------------------------------------------------------------------------
# MediaTypeDSK (RS232): CHS+fmttype -> byte offset math, file-backed
# round-trips, and the flat-IMG regression guard. FNIO_IS_STDIO makes a .DSK a
# plain stdio file; UNIT_TESTS turns the Debug_* macros into no-ops (so no
# util_debug_printf dependency needs linking).
# ------------------------------------------------------------------------------
add_executable(diskdsk_tests
    DiskTypeDSKTests.cpp
    ${CMAKE_SOURCE_DIR}/lib/media/rs232/diskTypeDSK.cpp
    ${CMAKE_SOURCE_DIR}/lib/media/rs232/diskType.cpp
)

target_compile_definitions(diskdsk_tests PRIVATE BUILD_RS232 FNIO_IS_STDIO UNIT_TESTS)

target_include_directories(diskdsk_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include/          # debug.h, global_types.h, fujiCommandID.h
    ${CMAKE_SOURCE_DIR}/lib/
    ${CMAKE_SOURCE_DIR}/lib/FileSystem/   # fnio.h
    ${CMAKE_SOURCE_DIR}/lib/utils/        # utils.h (parsed by diskType.cpp)
    ${CMAKE_SOURCE_DIR}/components_pc/
)

add_test(NAME diskdsk_tests COMMAND diskdsk_tests)

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
