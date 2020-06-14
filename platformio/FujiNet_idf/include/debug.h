#ifndef _DEBUG_H_
#define _DEBUG_H_

#include "../lib/hardware/fnUART.h"

/*
  Debugging Macros
*/
#ifdef DEBUG_S
    #ifdef NO_GLOBAL_SERIAL
        // Use FujiNet debug serial output
        #define Debug_print(...) fnUartDebug.print( __VA_ARGS__ )
        #define Debug_printf(...) fnUartDebug.printf( __VA_ARGS__ )
        #define Debug_println(...) fnUartDebug.println( __VA_ARGS__ )
    #else
        // Use Arduino debug serial output
        #define Debug_print(...) BUG_UART.print( __VA_ARGS__ )
        #define Debug_printf(...) BUG_UART.printf( __VA_ARGS__ )
        #define Debug_println(...) BUG_UART.println( __VA_ARGS__ )
    #endif
    #define DEBUG
#endif

#ifdef DEBUG_N
    #define Debug_print(...) wificlient.print( __VA_ARGS__ )
    #define Debug_printf(...) wificlient.printf( __VA_ARGS__ )
    #define Debug_println(...) wificlient.println( __VA_ARGS__ )
    #define DEBUG
#endif

#ifdef DEBUG
    #define HEAP_CHECK(x) Debug_printf("HEAP CHECK %s " x "\n", heap_caps_check_integrity_all(true) ? "PASSED":"FAILED")
#endif

#ifndef DEBUG
    #define Debug_print(...)
    #define Debug_printf(...)
    #define Debug_println(...)
#endif


#endif // _DEBUG_H_
