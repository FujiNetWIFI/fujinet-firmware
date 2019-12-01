/**
 * FujiNet Configuration Program
 */

#include <atari.h>
#include <stdbool.h>
#include "config.h"

void main(void)
{
  while (configured()==false)
    config_run();

  
}
