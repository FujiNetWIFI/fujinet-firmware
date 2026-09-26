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
# UTF-8 / HTML-entity folding to ASCII. fn_sanitize is deliberately free of any
# parser or protocol dependency, so the entity tables and the transliteration
# edge cases are pinned here rather than on hardware.
# ------------------------------------------------------------------------------
add_executable(fn_sanitize_tests
    FnSanitizeTests.cpp
    ${CMAKE_SOURCE_DIR}/lib/fntext/fn_sanitize.cpp
)

target_include_directories(fn_sanitize_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/
    ${CMAKE_SOURCE_DIR}/components_pc/   # doctest
)

add_test(NAME fn_sanitize_tests COMMAND fn_sanitize_tests)

# ------------------------------------------------------------------------------
# The XPath-subset query engine. Split out of FNXML precisely so it can be
# driven here with a literal document and no NetworkProtocol behind it.
# ------------------------------------------------------------------------------
add_executable(fnxml_query_tests
    FnxmlQueryTests.cpp
    ${CMAKE_SOURCE_DIR}/lib/fnxml/fnxml_query.cpp
    ${CMAKE_SOURCE_DIR}/lib/tinyxml2/tinyxml2.cpp
)

target_include_directories(fnxml_query_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/
    ${CMAKE_SOURCE_DIR}/lib/tinyxml2/
    ${CMAKE_SOURCE_DIR}/components_pc/   # doctest
)

add_test(NAME fnxml_query_tests COMMAND fnxml_query_tests)

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
            ${CMAKE_SOURCE_DIR}/lib/device/NDevice
)

if(FUJINET_TARGET STREQUAL "ATARI")
  add_executable(sio_dstats_tests
    sioNetwork-DSTATS-test.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/NDevice/NDevice.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/sio/sioNetwork.cpp
    )
  target_include_directories(sio_dstats_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include/
    ${CMAKE_SOURCE_DIR}/lib/
    ${CMAKE_SOURCE_DIR}/lib/bus/
    ${CMAKE_SOURCE_DIR}/lib/sam/
    ${CMAKE_SOURCE_DIR}/lib/device/
    ${CMAKE_SOURCE_DIR}/lib/config/
    ${CMAKE_SOURCE_DIR}/lib/printer-emulator/
    ${CMAKE_SOURCE_DIR}/lib/FileSystem/
    ${CMAKE_SOURCE_DIR}/lib/encoding/
    ${CMAKE_SOURCE_DIR}/lib/media/
    ${CMAKE_SOURCE_DIR}/lib/fuji/
    ${CMAKE_SOURCE_DIR}/lib/modem-sniffer/
    ${CMAKE_SOURCE_DIR}/lib/telnet/
    ${CMAKE_SOURCE_DIR}/lib/device/NDevice/
    ${CMAKE_SOURCE_DIR}/lib/device/fujiDevice/
    ${CMAKE_SOURCE_DIR}/lib/hardware/
    ${CMAKE_SOURCE_DIR}/lib/tcpip/
    ${CMAKE_SOURCE_DIR}/lib/compat/
    ${CMAKE_SOURCE_DIR}/lib/network-protocol/
    ${CMAKE_SOURCE_DIR}/lib/utils/
    ${CMAKE_SOURCE_DIR}/lib/ftp/
    ${CMAKE_SOURCE_DIR}/lib/webdav/
    ${CMAKE_SOURCE_DIR}/lib/http/
    ${CMAKE_SOURCE_DIR}/lib/TNFSlib/
    ${CMAKE_SOURCE_DIR}/lib/fnjson/
    ${CMAKE_SOURCE_DIR}/lib/fnhtml/
    ${CMAKE_SOURCE_DIR}/lib/fntext/
    ${CMAKE_SOURCE_DIR}/lib/fnxml/
    ${CMAKE_SOURCE_DIR}/components_pc/
    ${CMAKE_SOURCE_DIR}/components_pc/libssh/include/
    ${CMAKE_SOURCE_DIR}/build/components_pc/libssh/include/
    ${CMAKE_SOURCE_DIR}/components_pc/cJSON/
    ${CMAKE_SOURCE_DIR}/components_pc/mongoose/
    ${CMAKE_SOURCE_DIR}/components/gumbo-query/
    ${MBEDTLS_INCLUDE_DIR}
    )
  target_link_libraries(sio_dstats_tests PRIVATE)
  add_test(NAME sio_dstats_tests COMMAND sio_dstats_tests)

  # ----------------------------------------------------------------------------
  # The query output mode, end to end through a real parser: NET_SET_PARAMETER
  # flags, NET_QUERY, and the bytes that come back. XMLParser stands in for all
  # three modes - the flag plumbing and the ASCII fold are shared - and it is the
  # one whose library (tinyxml2) links without Gumbo or cJSON behind it.
  #
  # ATARI-only for the same reason as sio_dstats_tests: the bus headers need a
  # concrete systemBus.
  # ----------------------------------------------------------------------------
  add_executable(nquery_output_mode_tests
    NQueryOutputModeTests.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/NDevice/NParser.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/NDevice/XMLParser.cpp
    ${CMAKE_SOURCE_DIR}/lib/fntext/fn_sanitize.cpp
    ${CMAKE_SOURCE_DIR}/lib/fnxml/fnxml.cpp
    ${CMAKE_SOURCE_DIR}/lib/fnxml/fnxml_query.cpp
    ${CMAKE_SOURCE_DIR}/lib/tinyxml2/tinyxml2.cpp
    )
  target_include_directories(nquery_output_mode_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include/
    ${CMAKE_SOURCE_DIR}/lib/
    ${CMAKE_SOURCE_DIR}/lib/bus/
    ${CMAKE_SOURCE_DIR}/lib/sam/
    ${CMAKE_SOURCE_DIR}/lib/device/
    ${CMAKE_SOURCE_DIR}/lib/config/
    ${CMAKE_SOURCE_DIR}/lib/printer-emulator/
    ${CMAKE_SOURCE_DIR}/lib/FileSystem/
    ${CMAKE_SOURCE_DIR}/lib/encoding/
    ${CMAKE_SOURCE_DIR}/lib/media/
    ${CMAKE_SOURCE_DIR}/lib/fuji/
    ${CMAKE_SOURCE_DIR}/lib/modem-sniffer/
    ${CMAKE_SOURCE_DIR}/lib/telnet/
    ${CMAKE_SOURCE_DIR}/lib/device/NDevice/
    ${CMAKE_SOURCE_DIR}/lib/device/fujiDevice/
    ${CMAKE_SOURCE_DIR}/lib/hardware/
    ${CMAKE_SOURCE_DIR}/lib/tcpip/
    ${CMAKE_SOURCE_DIR}/lib/compat/
    ${CMAKE_SOURCE_DIR}/lib/network-protocol/
    ${CMAKE_SOURCE_DIR}/lib/utils/
    ${CMAKE_SOURCE_DIR}/lib/ftp/
    ${CMAKE_SOURCE_DIR}/lib/webdav/
    ${CMAKE_SOURCE_DIR}/lib/http/
    ${CMAKE_SOURCE_DIR}/lib/TNFSlib/
    ${CMAKE_SOURCE_DIR}/lib/fnjson/
    ${CMAKE_SOURCE_DIR}/lib/fnhtml/
    ${CMAKE_SOURCE_DIR}/lib/fntext/
    ${CMAKE_SOURCE_DIR}/lib/fnxml/
    ${CMAKE_SOURCE_DIR}/components_pc/
    ${CMAKE_SOURCE_DIR}/components_pc/libssh/include/
    ${CMAKE_SOURCE_DIR}/build/components_pc/libssh/include/
    ${CMAKE_SOURCE_DIR}/components_pc/cJSON/
    ${CMAKE_SOURCE_DIR}/components_pc/mongoose/
    ${CMAKE_SOURCE_DIR}/components/gumbo-query/
    ${MBEDTLS_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/lib/tinyxml2/
    )
  target_link_libraries(nquery_output_mode_tests PRIVATE)
  add_test(NAME nquery_output_mode_tests COMMAND nquery_output_mode_tests)
  add_executable(fsk_plan_tests
    FskPlanTests.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/sio/fsk_plan.cpp
  )

  target_include_directories(fsk_plan_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include/
    ${CMAKE_SOURCE_DIR}/lib/
    ${CMAKE_SOURCE_DIR}/lib/device/
    ${CMAKE_SOURCE_DIR}/lib/device/sio/
    ${CMAKE_SOURCE_DIR}/components_pc/
  )

  add_test(NAME fsk_plan_tests COMMAND fsk_plan_tests)

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
    ${CMAKE_SOURCE_DIR}/components_pc/
  )

  add_test(NAME cassette_time_plan_tests COMMAND cassette_time_plan_tests)

  # ----------------------------------------------------------------------------
  # Progressive raw-FSK loading (fsk_progressive.{h,cpp}): producer/consumer model, run in the tests
  # against the PRODUCTION RMT encoder callback and cursor seeding, which are extracted verbatim from
  # cassette.cpp at configure time (between the FSK_ENCODE_CB_* / FSK_SEED_* markers) and compiled into
  # the test around a small host stand-in for the sioCassette members they touch. Nothing of the
  # encoder is re-implemented in the test.
  # ----------------------------------------------------------------------------
  set(FSK_CASSETTE_SRC ${CMAKE_SOURCE_DIR}/lib/device/sio/cassette.cpp)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FSK_CASSETTE_SRC})
  file(READ ${FSK_CASSETTE_SRC} FSK_CASSETTE_TEXT)
  string(REPLACE "\r\n" "\n" FSK_CASSETTE_TEXT "${FSK_CASSETTE_TEXT}")

  function(fsk_extract_marked out_name begin_marker end_marker)
    string(FIND "${FSK_CASSETTE_TEXT}" "${begin_marker}" _b)
    string(FIND "${FSK_CASSETTE_TEXT}" "${end_marker}" _e)
    if(_b EQUAL -1 OR _e EQUAL -1 OR _e LESS _b)
      message(FATAL_ERROR "cassette.cpp: markers ${begin_marker} / ${end_marker} not found")
    endif()
    string(LENGTH "${begin_marker}" _bl)
    math(EXPR _start "${_b} + ${_bl}")
    math(EXPR _len "${_e} - ${_start}")
    string(SUBSTRING "${FSK_CASSETTE_TEXT}" ${_start} ${_len} _body)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/${out_name} "${_body}")
  endfunction()

  fsk_extract_marked(fsk_encode_cb.inc "// FSK_ENCODE_CB_BEGIN" "// FSK_ENCODE_CB_END")
  fsk_extract_marked(fsk_seed.inc "// FSK_SEED_BEGIN" "// FSK_SEED_END")

  add_executable(fsk_progressive_tests
    FskProgressiveTests.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/sio/fsk_progressive.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/sio/cassette_time_plan.cpp
    ${CMAKE_SOURCE_DIR}/lib/device/sio/fsk_plan.cpp
  )

  target_include_directories(fsk_progressive_tests PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/include/
    ${CMAKE_SOURCE_DIR}/lib/
    ${CMAKE_SOURCE_DIR}/lib/device/
    ${CMAKE_SOURCE_DIR}/lib/device/sio/
    ${CMAKE_SOURCE_DIR}/components_pc/
  )
  target_compile_definitions(fsk_progressive_tests PRIVATE
    FSK_PROGRESSIVE_TESTS_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  )
  find_package(Threads)
  if(Threads_FOUND)
    target_link_libraries(fsk_progressive_tests PRIVATE Threads::Threads)
    target_compile_definitions(fsk_progressive_tests PRIVATE FSK_PROGRESSIVE_TESTS_THREADS=1)
  endif()

  add_test(NAME fsk_progressive_tests COMMAND fsk_progressive_tests)
endif()

# ------------------------------------------------------------------------------
# Request header serialisation in the vendored HTTP client. A single header is
# never split across buffers, and getting that boundary wrong drops the blank
# line that ends the header block - the server then reads the body as headers
# (issue #1554, OneDrive's 2 KB bearer token vs the 512-byte default buffer).
# fn_http_header.cpp needs only esp_log.h/esp_err.h, stubbed here, so the
# boundary is cheap to pin down without hardware.
#
# lib/fn_esp_http_client/ is ESP-only and is not part of the fujinet-pc build;
# this target is the only thing that compiles it on a host. It needs BSD
# sys/queue.h, which mingw-w64 does not ship, so skip the test where that header
# is missing rather than vendor a copy - the code under test never runs there,
# and the Linux and macOS jobs still gate it.
# ------------------------------------------------------------------------------
include(CheckIncludeFileCXX)
check_include_file_cxx("sys/queue.h" HAVE_SYS_QUEUE_H)

if(HAVE_SYS_QUEUE_H)
  add_executable(http_header_tests
      HttpHeaderTests.cpp
      ${CMAKE_SOURCE_DIR}/lib/fn_esp_http_client/fn_http_header.cpp
      ${CMAKE_SOURCE_DIR}/lib/fn_esp_http_client/fn_http_utils.cpp
  )

  target_include_directories(http_header_tests PRIVATE
      ${CMAKE_SOURCE_DIR}/tests/esp_stubs/           # esp_log.h, esp_err.h
      ${CMAKE_SOURCE_DIR}/lib/fn_esp_http_client/
      ${CMAKE_SOURCE_DIR}/components_pc/             # doctest
  )

  add_test(NAME http_header_tests COMMAND http_header_tests)
else()
  message(STATUS "sys/queue.h not available - skipping http_header_tests")
endif()
