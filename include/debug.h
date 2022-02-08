#ifndef _DEBUG_H_
#define _DEBUG_H_

// __PLATFORMIO_BUILD_DEBUG__ is set when build_type is set to debug in platformio.ini
#if defined(__PLATFORMIO_BUILD_DEBUG__) || defined(DBUG2)
#define DEBUG
#endif

#ifdef UNIT_TESTS
#undef DEBUG
#endif

#include "../lib/hardware/fnUART.h"

/*
  Debugging Macros
*/
#ifdef DEBUG
    // Use FujiNet debug serial output
    #define Debug_print(...) fnUartDebug.print( __VA_ARGS__ )
    #define Debug_printf(...) fnUartDebug.printf( __VA_ARGS__ )
    #define Debug_println(...) fnUartDebug.println( __VA_ARGS__ )

    #define HEAP_CHECK(x) Debug_printf("HEAP CHECK %s " x "\n", heap_caps_check_integrity_all(true) ? "PASSED":"FAILED")
#endif

#ifndef DEBUG
    #define Debug_print(...)
    #define Debug_printf(...)
    #define Debug_println(...)

    #define HEAP_CHECK(x)
#endif

#endif // _DEBUG_H_
