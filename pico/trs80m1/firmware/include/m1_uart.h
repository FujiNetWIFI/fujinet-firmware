/* m1_uart.h -- the Radio Shack RS-232-C board at ports E8h-EBh (a TR1602 UART
 * and a BR1941 baud generator), as the FujiNet modem.
 *
 *   E8h  read: modem status (CTS, DSR, CD)    write: UART reset
 *   E9h  read: the sense switches             write: baud rates, RX low nibble
 *   EAh  read: UART status (DR, THRE, ...)    write: UART control
 *   EBh  read: the received byte              write: a byte to send
 *
 * Bytes from the modem reach the receive register no faster than the baud rate
 * the CPU set, and only once it has read the last, so nothing overruns.
 */
#ifndef M1_UART_H
#define M1_UART_H

#include <stdbool.h>
#include <stdint.h>

#include "fuji_ring.h"

#define M1_UART_BASE  0xE8
#define M1_UART_RING  256

#define M1_UART_MODEM 0xE0 /* CTS, DSR and CD: the modem is always there */
#define M1_UART_SENSE 0x6F /* 8 bits, no parity, one stop bit; unused bits float high */
#define M1_UART_DR    0x80
#define M1_UART_THRE  0x40
#define M1_UART_IDLE  0x07 /* unused status bits */

typedef struct {
    ring8_t inbound; /* link -> receive register */
    ring8_t outbound; /* EBh writes -> link */
    uint8_t holding;
    uint8_t ready; /* atomic: DR */
    uint8_t baud;  /* atomic: E9h as written */
    uint8_t ctrl;
    uint32_t next_rx_at; /* engine */
    uint8_t inbound_buf[M1_UART_RING];
    uint8_t outbound_buf[M1_UART_RING];
} m1_uart_t;

static inline uint8_t m1_uart_peek(m1_uart_t *uart, uint8_t port)
{
    switch (port & 3)
    {
    case 0:
        return M1_UART_MODEM;
    case 1:
        return M1_UART_SENSE;
    case 2:
        return (uint8_t)(M1_UART_IDLE |
                         (__atomic_load_n(&uart->ready, __ATOMIC_ACQUIRE) ? M1_UART_DR : 0) |
                         (ring8_space(&uart->outbound) ? M1_UART_THRE : 0));
    default:
        return uart->holding;
    }
}

static inline void m1_uart_after_read(m1_uart_t *uart, uint8_t port)
{
    if ((port & 3) == 3)
        __atomic_store_n(&uart->ready, 0, __ATOMIC_RELEASE);
}

static inline void m1_uart_write(m1_uart_t *uart, uint8_t port, uint8_t val)
{
    switch (port & 3)
    {
    case 0:
        __atomic_store_n(&uart->ready, 0, __ATOMIC_RELEASE);
        break;
    case 1:
        __atomic_store_n(&uart->baud, val, __ATOMIC_RELEASE);
        break;
    case 2:
        uart->ctrl = val;
        break;
    default:
        ring8_put(&uart->outbound, val); // full: dropped, as an unready UART would
        break;
    }
}

/* ---- engine ---- */

void m1_uart_init(m1_uart_t *uart);
/* Moves the next modem byte into the receive register when the baud rate allows. */
void m1_uart_service(m1_uart_t *uart, uint32_t now);

#endif /* M1_UART_H */
