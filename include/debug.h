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
    #include "../lib/hardware/fnUART.h"
    #define Serial fnUartDebug

    #define Debug_print(...) printf( __VA_ARGS__ )
    #define Debug_printf(format, ...) { printf( format, ##__VA_ARGS__ ); }
    #define Debug_println(...) { printf( __VA_ARGS__ ); printf( "\r\n" ); }
    #define Debug_printv(format, ...) { printf( ANSI_YELLOW "[%s:%d] %s(): " ANSI_GREEN_BOLD format ANSI_RESET "\r\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__);}

    #define HEAP_CHECK(x) Debug_printf("HEAP CHECK %s " x "\r\n", heap_caps_check_integrity_all(true) ? "PASSED":"FAILED")
    #define DEBUG_MEM_LEAK {Debug_printv("Heap[%lu] Low[%lu] Task[%u]", esp_get_free_heap_size(), esp_get_free_internal_heap_size(), uxTaskGetStackHighWaterMark(NULL));}
#else
    // Use util_debug_printf() helper function
    #include <utils.h>

    #define Debug_print(...) util_debug_printf(nullptr, __VA_ARGS__)
    #define Debug_printf(...) util_debug_printf(__VA_ARGS__)
    #define Debug_println(...) util_debug_printf("%s\r\n", __VA_ARGS__)
    #define Debug_printv(format, ...) {util_debug_printf( ANSI_YELLOW "[%s:%u] %s(): " ANSI_GREEN_BOLD format ANSI_RESET "\r\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__);}

    #define HEAP_CHECK(x) Debug_printf("HEAP CHECK %s " x "\r\n", heap_caps_check_integrity_all(true) ? "PASSED":"FAILED")
#endif
#endif // DEBUG

#ifndef DEBUG
#ifdef ESP_PLATFORM
    // Use FujiNet debug serial output
    #include "../lib/hardware/fnUART.h"
    #define Serial fnUartDebug
#endif

    #define Debug_print(...)
    #define Debug_printf(...)
    #define Debug_println(...)
    #define Debug_printv(format, ...)

    #define HEAP_CHECK(x)
#endif // !DEBUG

#endif // _DEBUG_H_
