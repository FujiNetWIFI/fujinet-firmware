# fujicart_checksram(<target> <core1 entry> [<symbol>...] [NO_CALLS]) -- after every link,
# fail the build unless core1's loop, everything it branches to and the named symbols are
# in SRAM (tools/checksram.py). Running at link time covers the ESP32 build too.

set(FUJICART_CHECKSRAM ${CMAKE_CURRENT_LIST_DIR}/../tools/checksram.py)

function(fujicart_checksram target entry)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    cmake_parse_arguments(ARG "NO_CALLS" "" "" ${ARGN})
    set(flags "")
    if(ARG_NO_CALLS)
        set(flags --no-calls)
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${FUJICART_CHECKSRAM} $<TARGET_FILE:${target}>
                ${entry} ${ARG_UNPARSED_ARGUMENTS} ${flags}
        VERBATIM)
endfunction()
