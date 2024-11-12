#ifdef BUILD_IEC

#include "_protocol.h"

#include "bus.h"

#include "../../../include/pinmap.h"
#include "../../../include/debug.h"

using namespace Protocol;


/**
 * Callback function to set timeout 
 */
static void onTimer(void *arg)
{
    IECProtocol *p = (IECProtocol *)arg;
    p->timer_timedout = true;
    //IEC.release( PIN_IEC_SRQ );
}

IECProtocol::IECProtocol() {
    esp_timer_create_args_t args = {
        .callback = onTimer,
        .arg = this,
	.dispatch_method = ESP_TIMER_TASK,
        .name = "onTimer",
	.skip_unhandled_events = 0,
    };
    esp_timer_create(&args, &timer_handle);
};

IECProtocol::~IECProtocol() {
    esp_timer_stop(timer_handle);
    esp_timer_delete(timer_handle);
};

/**
 * Start the timeout timer
 */
void IECProtocol::timer_start(uint64_t timeout_us)
{
    timer_timedout = false;
    timer_started = esp_timer_get_time();
    esp_timer_start_once(timer_handle, timeout_us);
    //IEC.pull( PIN_IEC_SRQ );
}
void IECProtocol::timer_stop()
{
    esp_timer_stop(timer_handle);
    timer_elapsed = esp_timer_get_time() - timer_started;
    //IEC.release( PIN_IEC_SRQ );
}

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
