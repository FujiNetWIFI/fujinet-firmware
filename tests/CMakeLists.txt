cmake_minimum_required(VERSION 3.16)

add_executable(fujibuspacket_tests
    FujiBusPacketTests.cpp
    ${CMAKE_SOURCE_DIR}/lib/bus/rs232/FujiBusPacket.cpp
)

target_include_directories(fujibuspacket_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include/      # for debug.h, etc.
    ${CMAKE_SOURCE_DIR}/lib/
    ${CMAKE_SOURCE_DIR}/components_pc/
)

target_link_libraries(fujibuspacket_tests PRIVATE
)

add_test(NAME fujibuspacket_tests COMMAND fujibuspacket_tests)

target_compile_definitions(fujibuspacket_tests PRIVATE
  FUJIBUS_TESTING
)
