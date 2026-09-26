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
# IMD (ImageDisk) core media type.
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
# The RS232 MediaTypeIMD adapter. BUILD_RS232 selects the rs232 media layer;
# the adapter deliberately includes nothing from lib/device or lib/bus, so the
# media sources alone link. See imdimage_tests above for UNIT_TESTS/FNIO_IS_STDIO.
# ------------------------------------------------------------------------------
add_executable(mediatypeimd_tests
    MediaTypeIMDTests.cpp
    ${CMAKE_SOURCE_DIR}/lib/media/IMDImage.cpp
    ${CMAKE_SOURCE_DIR}/lib/media/rs232/diskType.cpp
    ${CMAKE_SOURCE_DIR}/lib/media/rs232/diskTypeIMD.cpp
)

target_compile_definitions(mediatypeimd_tests PRIVATE BUILD_RS232 UNIT_TESTS FNIO_IS_STDIO)

target_include_directories(mediatypeimd_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include/      # debug.h, global_types.h
    ${CMAKE_SOURCE_DIR}/lib/
    ${CMAKE_SOURCE_DIR}/lib/FileSystem/
    ${CMAKE_SOURCE_DIR}/lib/media/
    ${CMAKE_SOURCE_DIR}/lib/media/rs232/
    ${CMAKE_SOURCE_DIR}/lib/utils/
    ${CMAKE_SOURCE_DIR}/components_pc/
)

add_test(NAME mediatypeimd_tests COMMAND mediatypeimd_tests)

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

# ------------------------------------------------------------------------------
# Policy check: media implementations must not access SYSTEM_BUS directly.
# The media layer should provide operations on the media itself (open, read,
# write, close, size, etc.) without knowing how those operations are requested.
# Bus/protocol handling belongs to the code handling the bus request.
# ------------------------------------------------------------------------------
add_test(
    NAME no_system_bus_in_media
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/check_no_system_bus.py
            ${CMAKE_SOURCE_DIR}/lib/media
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

# ------------------------------------------------------------------------------
# Macintosh GCR floppy encoder. Pure C++, no platform dependencies. Encodes a
# synthetic 800K disk and decodes every sector back with an independent
# decoder. Pass a 400K/800K .dsk on the command line to test a real image.
# ------------------------------------------------------------------------------
add_executable(mac_gcr_tests
    mac_gcr_test.cpp
    ${CMAKE_SOURCE_DIR}/lib/media/mac/macGCR.cpp
)
target_include_directories(mac_gcr_tests PRIVATE ${CMAKE_SOURCE_DIR}/lib/)
add_test(NAME mac_gcr_tests COMMAND mac_gcr_tests)

# ------------------------------------------------------------------------------
# lib/stuffit: unsit is a host CLI over the library. The sample tests compare
# it against unar and ndif2raw; they need -DSIT_SAMPLES_DIR / -DNDIF2RAW_DIR
# (or the same environment variables) and are skipped otherwise.
# ------------------------------------------------------------------------------
add_executable(unsit
    unsit.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/stuffit.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/sit_io.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/sit_crc16.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/sit_rle90.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/sit_lzw.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/sit_prefixcode.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/sit_huffman.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/sit_lzh13.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/sit_bwt.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/sit_arsenic.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/binhex.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/ndif.c
)
target_include_directories(unsit PRIVATE ${CMAKE_SOURCE_DIR}/lib/)

find_program(UNAR_EXECUTABLE unar)
if(NOT DEFINED SIT_SAMPLES_DIR)
  set(SIT_SAMPLES_DIR "$ENV{SIT_SAMPLES_DIR}")
endif()

if(UNAR_EXECUTABLE AND SIT_SAMPLES_DIR AND EXISTS "${SIT_SAMPLES_DIR}")
  add_test(NAME stuffit_sample_tests
    COMMAND ${CMAKE_COMMAND} -E env
      "UNSIT_BIN=$<TARGET_FILE:unsit>"
      "UNAR_BIN=${UNAR_EXECUTABLE}"
      "SIT_SAMPLES_DIR=${SIT_SAMPLES_DIR}"
      bash ${CMAKE_CURRENT_SOURCE_DIR}/stuffit_test.sh
  )
  set_tests_properties(stuffit_sample_tests PROPERTIES DEPENDS unsit)
else()
  message(STATUS "unar or SIT_SAMPLES_DIR not found - skipping stuffit_sample_tests")
endif()

add_executable(ndif_blocks_test
    ndif_blocks_test.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/stuffit.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/sit_io.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/sit_crc16.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/sit_rle90.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/sit_lzw.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/sit_prefixcode.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/sit_huffman.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/sit_lzh13.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/sit_bwt.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/sit_arsenic.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/binhex.c
    ${CMAKE_SOURCE_DIR}/lib/stuffit/ndif.c
)
target_include_directories(ndif_blocks_test PRIVATE ${CMAKE_SOURCE_DIR}/lib/)

find_program(NDIF2RAW_MAKE make)
if(NOT DEFINED NDIF2RAW_DIR)
  set(NDIF2RAW_DIR "$ENV{NDIF2RAW_DIR}")
endif()
set(NOW_SOFTWARE_SIT "${SIT_SAMPLES_DIR}/Now_Software.sit")

if(UNAR_EXECUTABLE AND NDIF2RAW_MAKE AND NDIF2RAW_DIR AND EXISTS "${NDIF2RAW_DIR}" AND EXISTS "${NOW_SOFTWARE_SIT}")
  add_test(NAME ndif_sample_tests
    COMMAND ${CMAKE_COMMAND} -E env
      "UNSIT_BIN=$<TARGET_FILE:unsit>"
      "NDIF_BLOCKS_TEST_BIN=$<TARGET_FILE:ndif_blocks_test>"
      "UNAR_BIN=${UNAR_EXECUTABLE}"
      "MAKE_BIN=${NDIF2RAW_MAKE}"
      "NDIF2RAW_DIR=${NDIF2RAW_DIR}"
      "NOW_SOFTWARE_SIT=${NOW_SOFTWARE_SIT}"
      bash ${CMAKE_CURRENT_SOURCE_DIR}/ndif_test.sh
  )
  set_tests_properties(ndif_sample_tests PROPERTIES DEPENDS unsit)
else()
  message(STATUS "unar, make, NDIF2RAW_DIR or Now_Software.sit not found - skipping ndif_sample_tests")
endif()
