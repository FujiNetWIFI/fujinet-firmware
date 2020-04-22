/*
  Debugging Macros
*/

#ifdef DEBUG_S
  #define Debug_print(...) BUG_UART.print( __VA_ARGS__ )
  #define Debug_printf(...) BUG_UART.printf( __VA_ARGS__ )
  #define Debug_println(...) BUG_UART.println( __VA_ARGS__ )
  #define DEBUG
#endif
#ifdef DEBUG_N
  #define Debug_print(...) wificlient.print( __VA_ARGS__ )
  #define Debug_printf(...) wificlient.printf( __VA_ARGS__ )
  #define Debug_println(...) wificlient.println( __VA_ARGS__ )
  #define DEBUG
#endif
#ifndef DEBUG
  #define Debug_print(...)
  #define Debug_printf(...)
  #define Debug_println(...)
#endif
