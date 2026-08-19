cmake_minimum_required(VERSION 3.13)

set(PROJECT Minty)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

# set boards directory
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)

# import board build options
include(${CMAKE_SOURCE_DIR}/boards/${PICO_BOARD}.cmake)

if(DEFINED ENV{PICO_SDK_PATH})
   include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)
else()
   include(pico_sdk_import.cmake)
endif()

project(${PROJECT} C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

include(FetchContent)

add_executable(${PROJECT})

#
# fetch no-OS-FatFS-SD-SDIO-SPI-RPi-Pico library
#
set(PICO_FATFS_DIR ${CMAKE_SOURCE_DIR}/lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico)
set(FATFS_CONFIG ${CMAKE_SOURCE_DIR}/include/ffconf.h)

FetchContent_Declare(
    pico_fatfs
    GIT_REPOSITORY https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
    GIT_TAG v3.6.2
    SOURCE_DIR ${PICO_FATFS_DIR}
)

FetchContent_MakeAvailable(pico_fatfs)

add_subdirectory(
   ${PICO_FATFS_DIR}/src
   ${PICO_FATFS_DIR}/build
)

#
# overwrite sd_card_spi.c to support SD <=2GB
#
file(COPY
    ${CMAKE_SOURCE_DIR}/src/sd_card_spi.c
    DESTINATION ${PICO_FATFS_DIR}/src/sd_driver/SPI
)

# disable warning on f_util.c
set_source_files_properties(
   ${PICO_FATFS_DIR}/src/src/f_util.c
   PROPERTIES COMPILE_OPTIONS "-Wno-format"
)

# exclude glue.c (-> diskio.c)
set_source_files_properties(
    ${PICO_FATFS_DIR}/src/src/glue.c
    PROPERTIES HEADER_FILE_ONLY TRUE
)
###

#
# fetch LittleFS library
#
set(LITTLEFS_DIR ${CMAKE_SOURCE_DIR}/lib/littlefs)

FetchContent_Declare(
    littlefs
    GIT_REPOSITORY https://github.com/littlefs-project/littlefs.git
    GIT_TAG master
    SOURCE_DIR ${LITTLEFS_DIR}
)

FetchContent_MakeAvailable(littlefs)
###

# set version header file
execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

string(REPLACE "." ";" VERSION_LIST "${GIT_TAG}")

if(VERSION_LIST)
   list(GET VERSION_LIST 0 VERSION_MAJOR)
   list(GET VERSION_LIST 1 VERSION_MINOR)
else()
   set(VERSION_MAJOR 0)
   set(VERSION_MINOR 0)
endif()

configure_file(${CMAKE_SOURCE_DIR}/include/version.h.in ${CMAKE_SOURCE_DIR}/include/version.h @ONLY)

set_target_properties(Minty PROPERTIES
   OUTPUT_NAME "Minty_${PICO_BOARD}${OUTPUT_SUFFIX}"
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
   target_link_libraries(${PROJECT} pico_stdio_uart)
   pico_enable_stdio_uart(${PROJECT} 1)
   pico_enable_stdio_usb(${PROJECT} 1)
endif()

if(CONFIG_USB_DEVICE)
   target_compile_options(${PROJECT} PRIVATE "-DCFG_TUSB_RHPORT0_MODE")
   target_link_libraries(${PROJECT} tinyusb_device)
   target_sources(${PROJECT} PUBLIC
      ${CMAKE_CURRENT_LIST_DIR}/src/usb_tasks.c
      ${CMAKE_CURRENT_LIST_DIR}/src/usb_descriptors.c
   )
endif()

if(CONFIG_USB_DEVICE AND CONFIG_FLASH_FAT_STORAGE)
   target_sources(${PROJECT} PUBLIC
      ${CMAKE_CURRENT_LIST_DIR}/src/msc_disk.c
   )
endif()

if(CONFIG_FLASH_FAT_STORAGE)
   target_sources(${PROJECT} PUBLIC
      ${CMAKE_CURRENT_LIST_DIR}/src/fatfs_disk.c
      ${CMAKE_CURRENT_LIST_DIR}/src/flash_fs.c
   )
elseif(CONFIG_FLASH_LFS_STORAGE)
   target_sources(${PROJECT} PUBLIC
      ${CMAKE_CURRENT_LIST_DIR}/src/pico_lfs.c
      ${CMAKE_CURRENT_LIST_DIR}/src/littlefs_backend.c
      ${LITTLEFS_DIR}/lfs.c
      ${LITTLEFS_DIR}/lfs_util.c
   )

   # disable warning on lfs.c
   set_source_files_properties(
      ${LITTLEFS_DIR}/lfs.c
      PROPERTIES COMPILE_OPTIONS "-Wno-unused-function"
   )

   target_compile_definitions(${PROJECT} PRIVATE "LFS_NO_DEBUG=1")
   target_compile_definitions(${PROJECT} PRIVATE "LFS_THREADSAFE=1")

   target_include_directories(${PROJECT} PUBLIC
       ${LITTLEFS_DIR}
   )
endif()

if(CONFIG_ECS_AUDIO OR CONFIG_INTELLIVOICE)
   target_sources(${PROJECT} PUBLIC
      ${CMAKE_CURRENT_LIST_DIR}/src/audio.c
      ${CMAKE_CURRENT_LIST_DIR}/src/emu2149.c
      ${CMAKE_CURRENT_LIST_DIR}/src/ivoice.c
   )
   target_link_libraries(${PROJECT} hardware_pwm)
endif()

if(CONFIG_JLP)
   target_sources(${PROJECT} PUBLIC
      ${CMAKE_CURRENT_LIST_DIR}/src/jlpflash.c
   )
endif()

if(CONFIG_FUJINET)
   target_sources(${PROJECT} PUBLIC
      ${CMAKE_CURRENT_LIST_DIR}/src/fujibus.c
      ${CMAKE_CURRENT_LIST_DIR}/src/fujibus_usb.c
      ${CMAKE_CURRENT_LIST_DIR}/src/fujinet.c
      ${CMAKE_CURRENT_LIST_DIR}/src/fujiboot.c
   )
endif()

if(PICO_RP2350)
   target_link_libraries(${PROJECT} pico_sha256)
endif()

set(OUTPUT_SUFFIX "" CACHE STRING "Suffix for output name")

target_compile_definitions(${PROJECT} PRIVATE "PICO_FLASH_SIZE_BYTES=${PICO_FLASH_SIZE_BYTES}")
target_compile_definitions(${PROJECT} PRIVATE "PICO_FLASH_SPI_CLKDIV=${PICO_FLASH_SPI_CLKDIV}")
target_compile_definitions(${PROJECT} PRIVATE CONFIG_USB_DEVICE=$<BOOL:${CONFIG_USB_DEVICE}>)
target_compile_definitions(${PROJECT} PRIVATE CONFIG_SD_STORAGE=$<BOOL:${CONFIG_SD_STORAGE}>)
target_compile_definitions(${PROJECT} PRIVATE CONFIG_FLASH_FAT_STORAGE=$<BOOL:${CONFIG_FLASH_FAT_STORAGE}>)
target_compile_definitions(${PROJECT} PRIVATE CONFIG_FLASH_LFS_STORAGE=$<BOOL:${CONFIG_FLASH_LFS_STORAGE}>)
target_compile_definitions(${PROJECT} PRIVATE CONFIG_JLP=$<BOOL:${CONFIG_JLP}>)
target_compile_definitions(${PROJECT} PRIVATE CONFIG_ECS_AUDIO=$<BOOL:${CONFIG_ECS_AUDIO}>)
target_compile_definitions(${PROJECT} PRIVATE CONFIG_INTELLIVOICE=$<BOOL:${CONFIG_INTELLIVOICE}>)
target_compile_definitions(${PROJECT} PRIVATE CONFIG_FUJINET=$<BOOL:${CONFIG_FUJINET}>)
target_compile_definitions(${PROJECT} PRIVATE "MAX_ROM_SIZE=${MAX_ROM_SIZE}")
target_compile_definitions(${PROJECT} PRIVATE "MINTY_FW=1")

# options to enable FatFS SD debug
#target_compile_definitions(${PROJECT} PRIVATE "USE_PRINTF=1")
#target_compile_definitions(${PROJECT} PRIVATE "USE_DBG_PRINTF=1")

target_compile_options(${PROJECT} PRIVATE "-O2")
target_compile_options(${PROJECT} PRIVATE "-Wall")

set_property(TARGET ${PROJECT} APPEND_STRING PROPERTY LINK_FLAGS "-Wl,--print-memory-usage")

target_sources(${PROJECT} PUBLIC
   ${CMAKE_CURRENT_LIST_DIR}/src/main.c
   ${CMAKE_CURRENT_LIST_DIR}/src/cartridge.c
   ${CMAKE_CURRENT_LIST_DIR}/src/interface.c
   ${CMAKE_CURRENT_LIST_DIR}/src/filesystem.c
   ${CMAKE_CURRENT_LIST_DIR}/src/intellicart.c
   ${CMAKE_CURRENT_LIST_DIR}/src/memory.c
   ${CMAKE_CURRENT_LIST_DIR}/src/diskio.c
   ${CMAKE_CURRENT_LIST_DIR}/src/hw_config.c
   ${CMAKE_CURRENT_LIST_DIR}/src/utils.c
   ${CMAKE_CURRENT_LIST_DIR}/src/vfs.c
   ${CMAKE_CURRENT_LIST_DIR}/src/fatfs_backend.c
   ${CMAKE_CURRENT_LIST_DIR}/src/rommeta_parser.c
)

target_include_directories(${PROJECT} PUBLIC
   ${CMAKE_CURRENT_LIST_DIR}/include
)

###

target_link_libraries(${PROJECT}
   pico_stdlib
   hardware_flash 
   pico_multicore
   pico_rand
   no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(${PROJECT})
