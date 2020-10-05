#include <string.h>
#include <stdint.h>
#include "test_buffer.h"

/**
 * External buffers for protocol instantiation (tests_main.c)
 */
extern uint8_t *rx_buf;
extern uint8_t *tx_buf;
extern uint8_t *sp_buf;

void test_buffer_setup(const char *c)
{
    // Clear buffers
    memset(rx_buf, 0, RX_TX_CAPACITY);
    memset(tx_buf, 0, RX_TX_CAPACITY);

    // Copy into buffers
    memcpy(rx_buf, c, strlen(c));
    memcpy(tx_buf, c, strlen(c));
}