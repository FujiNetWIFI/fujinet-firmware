#ifndef FNHWTIMER_H
#define FNHWTIMER_H

#include "esp_idf_version.h"
#include "driver/timer.h" // contains the hardware timer register data structure
#include "soc/soc.h"
#include "soc/timer_group_struct.h"

// hardware timer parameters for bit-banging I/O
#define TIMER_DIVIDER         (2)  //  Hardware timer clock divider
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#define TIMER_SCALE           (APB_CLK_FREQ / TIMER_DIVIDER)  // convert counter value to seconds
#else
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#endif
#define TIMER_100NS_FACTOR    (TIMER_SCALE / 10000000)
#define TIMER_ADJUST          0 // substract this value to adjust for overhead

class HardwareTimer
{
private:
  struct fn_timer_t
  {
    uint32_t tn;
    uint32_t t0;
  } fn_timer;

timer_config_t fn_config;

public:
  void config();
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  void reset() { TIMERG1.hw_timer[TIMER_1].loadlo.val = 0; TIMERG1.hw_timer[TIMER_1].load.val = 0; };
  void latch() { TIMERG1.hw_timer[TIMER_1].update.val = 0; };
  void read() { fn_timer.t0 = TIMERG1.hw_timer[TIMER_1].lo.val; };
#else
  void reset() { TIMERG1.hw_timer[TIMER_1].load_low = 0; TIMERG1.hw_timer[TIMER_1].reload = 0; };
  void latch() { TIMERG1.hw_timer[TIMER_1].update = 0; };
  void read() { fn_timer.t0 = TIMERG1.hw_timer[TIMER_1].cnt_low; };
#endif
  bool timeout() { return (fn_timer.t0 > fn_timer.tn); };
  void wait() { do{latch(); read();} while (!timeout()); };
  void alarm_set(int s) { fn_timer.tn = fn_timer.t0 + s * TIMER_100NS_FACTOR - TIMER_ADJUST; };
  void alarm_snooze(int s) { fn_timer.tn += s * TIMER_100NS_FACTOR - TIMER_ADJUST; };
};

extern HardwareTimer fnTimer;

#endif // FNHWTIMER_H