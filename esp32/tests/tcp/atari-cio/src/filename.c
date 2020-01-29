/**
 * Filename processing function
 */

#include <atari.h>
#include <string.h>
#include "filename.h"

unsigned char aux1_save[8];
unsigned char aux2_save[8];

/**
 * Save aux values
 */
void aux_save(unsigned char d)
{
  aux1_save[d]=d;
  aux2_save[d]=d;
}
