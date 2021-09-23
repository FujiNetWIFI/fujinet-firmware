#ifndef ADAMNET_H
#define ADAMNET_H

/**
 * AdamNet Routines
 */

#include <forward_list>
#include "fnSystem.h"

#define MN_RESET    0x00    // command.control (reset)
#define MN_STATUS   0x01    // command.control (status)
#define MN_ACK      0x02    // command.control (ack)
#define MN_CLR      0x03    // command.control (clr) (aka CTS)
#define MN_RECEIVE  0x04    // command.control (receive)
#define MN_CANCEL   0x05    // command.control (cancel)
#define MN_SEND     0x06    // command.control (send)
#define MN_NACK     0x07    // command.control (nack)
#define MN_READY    0x0D    // command.control (ready)

#define NM_STATUS   0x08    // response.control (status)
#define NM_ACK      0x09    // response.control (ack)
#define NM_CANCEL   0x0A    // response.control (cancel)
#define NM_SEND     0x0B    // response.data (send)
#define NM_NACK     0x0C    // response.control (nack)

/**
 * Calculate checksum for AdamNet packets
 * @brief uses a simple 8-bit XOR of each successive byte.
 * @param buf pointer to buffer
 * @param len length of buffer
 * @return checksum value (0x00 - 0xFF)
 */
uint8_t adamnet_checksum(uint8_t *buf, unsigned short len);



#endif /* ADAMNET_H */