/**
 * PLATOTERM for Atari Cartridges
 *
 * Author: Thomas Cherryhomes <thom.cherryhomes at gmail dot com>
 *
 * I/O Functions
 */

#ifndef IO_H
#define IO_H

#define XON  0x11
#define XOFF 0x13

#include <stdint.h>

/**
 * io_init() - Set-up the I/O
 */
void io_init(void);

/**
 * io_send_byte(b) - Send specified byte out
 */
void io_send_byte(uint8_t b);

/**
 * io_main() - The IO main loop
 */
void io_main(void);

/**
 * io_done() - Called to close I/O
 */
void io_done(void);

/**
 * io_baud - set baud rate
 */
void io_baud(unsigned char new_baud);

/**
 * io_xon - send XON
 */
void io_xon(void);

/**
 * io_xoff - send XON
 */
void io_xoff(void);

#endif /* IO_H */
