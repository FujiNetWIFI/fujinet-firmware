#ifdef BUILD_CDC

#include "fuji.h"

cdcFuji theFuji; // Global fuji object.

cdcFuji::cdcFuji()
{
  // Helpful for debugging
  for (int i = 0; i < MAX_HOSTS; i++)
    _fnHosts[i].slotid = i;
}

#endif /* BUILD_CDC */