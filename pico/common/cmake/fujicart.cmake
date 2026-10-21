# fujicart.cmake -- fujicart_firmware(), what every cart's firmware/CMakeLists.txt builds
# with after pico_sdk_init(). fujicart_pre.cmake includes this.
#
#   fujicart_firmware(<target>
#       SOURCES <file>...                 in link order
#       [USB_PRODUCT <name>]              the USB product string
#       [LIBRARIES <lib>...]              SDK libraries beyond stdlib, multicore and TinyUSB
#       [PIO <file.pio>...]
#       [COPY_TO_RAM]                     run the whole image from SRAM
#       [OPTIMIZE <flag>]                 default -Os
#       [CORE1 <entry> [<symbol>...]]     checked after the link (fujicart_checksram)
#       [CORE1_NO_CALLS])

include(${CMAKE_CURRENT_LIST_DIR}/fujicart_checksram.cmake)

function(fujicart_firmware target)
    cmake_parse_arguments(ARG "COPY_TO_RAM;CORE1_NO_CALLS" "USB_PRODUCT;OPTIMIZE"
                          "SOURCES;LIBRARIES;PIO;CORE1" ${ARGN})
    if(NOT ARG_OPTIMIZE)
        set(ARG_OPTIMIZE -Os)
    endif()

    add_executable(${target} ${ARG_SOURCES})
    foreach(pio ${ARG_PIO})
        pico_generate_pio_header(${target} ${CMAKE_CURRENT_SOURCE_DIR}/${pio})
    endforeach()
    target_compile_options(${target} PRIVATE ${ARG_OPTIMIZE} -Wall)
    if(ARG_USB_PRODUCT)
        target_compile_definitions(${target} PRIVATE FUJI_USB_PRODUCT="${ARG_USB_PRODUCT}")
    endif()
    # build-cart.sh's fresh headers first, the committed ones last
    target_include_directories(${target} PRIVATE
        ${FUJI_GEN}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/baked
        ${FUJI_COMMON}/include
        ${FUJI_COMMON}/usb
    )
    target_link_libraries(${target} PRIVATE
        pico_stdlib pico_multicore ${ARG_LIBRARIES} tinyusb_device)
    if(ARG_COPY_TO_RAM)
        pico_set_binary_type(${target} copy_to_ram)
    endif()
    pico_enable_stdio_uart(${target} 0)
    pico_enable_stdio_usb(${target} 0)
    pico_add_extra_outputs(${target})

    if(ARG_CORE1)
        if(ARG_CORE1_NO_CALLS)
            fujicart_checksram(${target} ${ARG_CORE1} NO_CALLS)
        else()
            fujicart_checksram(${target} ${ARG_CORE1})
        endif()
    endif()
endfunction()
