
#ifndef MISC_H
#define MISC_H

// EOL modes
#define CR 1
#define LF 2
#define CRLF 3

/**
 * Save aux values
 */
void aux_save(unsigned char d);

/**
 * Return lo byte of 16-bit value
 */
unsigned char lo(unsigned short w);

/**
 * Return hi byte of 16-bit value
 */
unsigned char hi(unsigned short w);

/**
 * Clear RX buffer
 */
void clear_rx_buffer(void);

/**
 * Clear TX buffer
 */
void clear_tx_buffer(void);

/**
 * Save ZP
 */
void zp_save(void);

/**
 * Restore ZP
 */
void zp_restore(void);

#endif /* MISC_H */
