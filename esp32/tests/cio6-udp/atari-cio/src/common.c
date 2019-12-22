/**
 * CIO common functions
 */

#include "common.h"

/**
 * Convert aux1/aux2 8 bit pairs to 16-bit number
 */

unsigned short aux12_to_aux(unsigned char aux1, unsigned char aux2)
{
  return (aux2 << 8) + aux1;
}
