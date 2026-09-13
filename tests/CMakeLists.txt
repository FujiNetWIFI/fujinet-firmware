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
# Calendar date arithmetic and event-draft parsing. fn_time and calendar_draft
# are deliberately free of libc time functions and of any FujiNet dependency, so
# they link on their own - which is what makes the DST and parsing edge cases
# cheap to pin down here rather than on hardware.
# ------------------------------------------------------------------------------
add_executable(calendar_tests
    CalendarTests.cpp
    ${CMAKE_SOURCE_DIR}/lib/utils/fn_time.cpp
    ${CMAKE_SOURCE_DIR}/lib/network-protocol/calendar_draft.cpp
)

target_include_directories(calendar_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include/
    ${CMAKE_SOURCE_DIR}/lib/
    ${CMAKE_SOURCE_DIR}/components_pc/   # doctest
)

add_test(NAME calendar_tests COMMAND calendar_tests)

# ------------------------------------------------------------------------------
# Mail draft parsing and RFC822 assembly (compose / reply). mail_draft depends
# only on the standard library, so the EOL and threading edge cases are pinned
# here rather than on hardware.
# ------------------------------------------------------------------------------
add_executable(mail_tests
    MailDraftTests.cpp
    ${CMAKE_SOURCE_DIR}/lib/network-protocol/mail_draft.cpp
)

target_include_directories(mail_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include/
    ${CMAKE_SOURCE_DIR}/lib/
    ${CMAKE_SOURCE_DIR}/components_pc/   # doctest
)

add_test(NAME mail_tests COMMAND mail_tests)

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

# ------------------------------------------------------------------------------
# Pure A8CAS FSK planning/preload module (lib/device/sio/fsk_plan). It is free of
# FujiNet hardware, ESP-IDF, RMT/GPIO, filesystem, and global state, so it links
# standalone on the host - which is what makes the decode/parity/scale/split,
# segmented block-table addressing, and bounded preload edge cases cheap to pin
# down here rather than on hardware.
# ------------------------------------------------------------------------------
add_executable(fsk_plan_tests
    FskPlanTests.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/sio/fsk_plan.cpp
)

target_include_directories(fsk_plan_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include/
    ${CMAKE_SOURCE_DIR}/lib/
    ${CMAKE_SOURCE_DIR}/lib/device/
    ${CMAKE_SOURCE_DIR}/lib/device/sio/
    ${CMAKE_SOURCE_DIR}/components_pc/   # doctest
)

add_test(NAME fsk_plan_tests COMMAND fsk_plan_tests)

# ------------------------------------------------------------------------------
# Pure Custom Rewind time-model module (lib/device/sio/cassette_time_plan). Same
# host-buildable discipline as fsk_plan above (no FujiNet/ESP-IDF/hardware
# dependency) and reuses fsk_plan.cpp directly for the FSK duration primitives,
# so the two modules can never diverge on FSK timing.
# ------------------------------------------------------------------------------
add_executable(cassette_time_plan_tests
    CassetteTimePlanTests.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/sio/cassette_time_plan.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/sio/fsk_plan.cpp
)

target_include_directories(cassette_time_plan_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include/
    ${CMAKE_SOURCE_DIR}/lib/
    ${CMAKE_SOURCE_DIR}/lib/device/
    ${CMAKE_SOURCE_DIR}/lib/device/sio/
    ${CMAKE_SOURCE_DIR}/components_pc/   # doctest
)

add_test(NAME cassette_time_plan_tests COMMAND cassette_time_plan_tests)
