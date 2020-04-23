/**
 * Filename processing function
 */

#include <atari.h>
#include <string.h>
#include "misc.h"
#include "config.h"

extern unsigned char buffer_rx_len[MAX_DEVICES];
extern unsigned char buffer_tx_len[MAX_DEVICES];
extern unsigned char buffer_tx[MAX_DEVICES][256];
extern unsigned char buffer_rx[MAX_DEVICES][256];
extern unsigned char* tp[MAX_DEVICES];
extern unsigned char* rp[MAX_DEVICES];

unsigned char aux1_save[MAX_DEVICES];
unsigned char aux2_save[MAX_DEVICES];

/**
 * Save aux values
 */
void aux_save(unsigned char d)
{
  aux1_save[d]=OS.ziocb.aux1;
  aux2_save[d]=OS.ziocb.aux2;
}

/**
 * Return lo byte of 16-bit value
 */
unsigned char lo(unsigned short w)
{
  return (w&0xFF);
}

/**
 * Return hi byte of 16-bit value
 */
unsigned char hi(unsigned short w)
{
  return (w>>8);
}

/**
 * Clear RX buffer
 */
void clear_rx_buffer(void)
{
  memset(&buffer_rx[OS.ziocb.drive-1],0x00,sizeof(buffer_rx[OS.ziocb.drive-1]));
  buffer_rx_len[OS.ziocb.drive-1]=0;
  rp[OS.ziocb.drive-1]=&buffer_rx[OS.ziocb.drive-1][0];
}

/**
 * Clear TX buffer
 */
void clear_tx_buffer(void)
{
  memset(&buffer_tx[OS.ziocb.drive-1],0x00,sizeof(buffer_tx[OS.ziocb.drive-1]));
  buffer_tx_len[OS.ziocb.drive-1]=0;
  tp[OS.ziocb.drive-1]=&buffer_rx[OS.ziocb.drive-1][0];
}
