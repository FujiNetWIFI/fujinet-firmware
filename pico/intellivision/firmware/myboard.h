// myboard.h

// setting first overrides the value in the default header

//#define PICO_FLASH_SPI_CLKDIV 2     // use for winbond flash
#define PICO_FLASH_SPI_CLKDIV 4     // use for slower flash (e.g. zbit)

// Debug console: TX-only on GPIO28. Every other UART0/UART1 pin is either
// part of the CP-1610 bus (GPIO0-20) or the onboard LED (GPIO25); there is
// no free TX/RX pair. PICO_DEFAULT_UART_RX_PIN is deliberately left
// undefined -- stdio_uart_init() takes the TX-only path when it's absent.
#define PICO_DEFAULT_UART 0
#define PICO_DEFAULT_UART_TX_PIN 28

// pick up the rest of the settings
#include "boards/pico.h"
