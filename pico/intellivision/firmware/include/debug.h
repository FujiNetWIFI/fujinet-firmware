#ifndef DEBUG_H
#define DEBUG_H

#ifndef NDEBUG

// enable UART console on MI_DBG_UART_ID (MI_DBG_UART_TX_PIN / MI_DBG_UART_RX_PIN)
#define PICO_UART_CONSOLE

#endif      // NDEBUG

#endif
