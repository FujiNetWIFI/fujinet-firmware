#ifdef BUILD_IEC

#include "_protocol.h"

#include "bus.h"

#include "../../../include/pinmap.h"
#include "../../../include/debug.h"

using namespace Protocol;

int IRAM_ATTR IECProtocol::waitForSignals(int pin1, int state1,
					  int pin2, int state2,
					  int timeout)
{
  uint64_t start, now, elapsed;
  int abort = 0;

  start = esp_timer_get_time();
  for (;;) {
    if (IEC_IS_ASSERTED(pin1) == state1)
      break;
    if (pin2 && IEC_IS_ASSERTED(pin2) == state2)
      break;

    now = esp_timer_get_time();
    elapsed = now - start;
    if (elapsed >= timeout) {
      abort = 1;
      break;
    }
  }

  return abort ? TIMED_OUT : 0;
}

void IECProtocol::transferDelaySinceLast(size_t minimumDelay)
{
  uint64_t now, elapsed;
  int64_t remaining;


  now = esp_timer_get_time();
  elapsed = now - _transferEnded;
  if (minimumDelay > 0) {
    remaining = minimumDelay;
    remaining -= elapsed;
    if (remaining > 0) {
      usleep(remaining);
      now = esp_timer_get_time();
    }
  }

  _transferEnded = now;
  return;
}

#endif /* BUILD_IEC */
