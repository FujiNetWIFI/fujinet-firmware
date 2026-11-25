// See m1_uart.h.
#include "m1_uart.h"

#include <string.h>

// The BR1941's rates, by the nibble written to E9h, in tenths of a baud.
static const uint32_t baud_tenths[16] = {500,   750,   1100,  1345,  1500,  3000,
                                         6000,  12000, 18000, 20000, 24000, 36000,
                                         48000, 72000, 96000, 192000};

void m1_uart_init(m1_uart_t *uart)
{
    memset(uart, 0, sizeof(*uart));
    ring8_init(&uart->inbound, uart->inbound_buf, M1_UART_RING);
    ring8_init(&uart->outbound, uart->outbound_buf, M1_UART_RING);
    uart->baud = 0xEE; // 9600 both ways until the CPU sets it
}

void m1_uart_service(m1_uart_t *uart, uint32_t now)
{
    if (__atomic_load_n(&uart->ready, __ATOMIC_ACQUIRE) || (int32_t)(now - uart->next_rx_at) < 0)
        return;
    uint8_t byte;
    if (!ring8_get(&uart->inbound, &byte))
        return;
    uart->holding = byte;
    __atomic_store_n(&uart->ready, 1, __ATOMIC_RELEASE);
    // Ten bits a character: a start bit, eight data bits and a stop bit.
    uint32_t tenths = baud_tenths[__atomic_load_n(&uart->baud, __ATOMIC_ACQUIRE) & 0x0F];
    uart->next_rx_at = now + 100000000u / tenths;
}
