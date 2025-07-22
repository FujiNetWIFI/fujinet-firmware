#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <string>

#include "ansi_codes.h"

// __PLATFORMIO_BUILD_DEBUG__ is set when build_type is set to debug in platformio.ini
// __PC_BUILD_DEBUG__ is set when build_type is set Debug in in fujinet_pc.cmake
// DBUG2 is set to enable monitor messages for a release build
//       in platformio.ini for ESP build, in fujinet_pc.cmake for PC build
#if defined(__PLATFORMIO_BUILD_DEBUG__) || defined(__PC_BUILD_DEBUG__) || defined(DBUG2)
#define DEBUG
#endif

#ifdef UNIT_TESTS
#undef DEBUG
#endif

/*
  Debugging Macros
*/
#ifdef DEBUG
#ifdef ESP_PLATFORM
    // Use FujiNet debug serial output
    #include "../lib/hardware/UARTChannel.h"
    #define Serial fnDebugConsole
#ifdef PINMAP_RS232_S3
    #define Debug_print(...) fnDebugConsole.printf( __VA_ARGS__ )
    #define Debug_printf(...) fnDebugConsole.printf( __VA_ARGS__ )
    #define Debug_println(...) do { fnDebugConsole.printf(__VA_ARGS__); printf("\n"); } while (0)
    #define Debug_printv(format, ...) { fnDebugConsole.printf( ANSI_YELLOW "[%s:%u] %s(): " ANSI_GREEN_BOLD format ANSI_RESET "\r\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__);}
#else
    #define Debug_print(...) fnDebugConsole.print( __VA_ARGS__ )
    #define Debug_printf(...) fnDebugConsole.printf( __VA_ARGS__ )
    #define Debug_println(...) fnDebugConsole.println( __VA_ARGS__ )
    #define Debug_printv(format, ...) {fnDebugConsole.printf( ANSI_YELLOW "[%s:%u] %s(): " ANSI_GREEN_BOLD format ANSI_RESET "\r\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__);}
#endif // PINMAP_RS232_S3

    #define HEAP_CHECK(x) Debug_printf("HEAP CHECK %s " x "\r\n", heap_caps_check_integrity_all(true) ? "PASSED":"FAILED")
#else
    // Use util_debug_printf() helper function
    #include <utils.h>

    #define Debug_print(...) util_debug_printf(nullptr, __VA_ARGS__)
    #define Debug_printf(...) util_debug_printf(__VA_ARGS__)
    #define Debug_println(...) util_debug_printf("%s\r\n", __VA_ARGS__)
    #define Debug_printv(format, ...) {util_debug_printf( ANSI_YELLOW "[%s:%u] %s(): " ANSI_GREEN_BOLD format ANSI_RESET "\r\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__);}

    #define HEAP_CHECK(x) Debug_printf("HEAP CHECK %s " x "\r\n", heap_caps_check_integrity_all(true) ? "PASSED":"FAILED")
#endif // ESP_PLATFORM
#endif // DEBUG

#ifndef DEBUG
#ifdef ESP_PLATFORM
    // Use FujiNet debug serial output
    #include "../lib/hardware/fnUART.h"
    #define Serial fnDebugConsole
#endif

    #define Debug_print(...)
    #define Debug_printf(...)
    #define Debug_println(...)
    #define Debug_printv(format, ...)

    #define HEAP_CHECK(x)
#endif // !DEBUG

#endif // _DEBUG_H_
