#ifndef FNHWTIMER_H
#define FNHWTIMER_H

#include "driver/timer.h" // contains the hardware timer register data structure

// hardware timer parameters for bit-banging I/O
#define TIMER_DIVIDER         (2)  //  Hardware timer clock divider
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
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

public:
  void config();
  void reset() { TIMERG1.hw_timer[TIMER_1].load_low = 0; TIMERG1.hw_timer[TIMER_1].reload = 0;};
  void wait() { do{latch(); read();} while (!test()); };
  void latch() { TIMERG1.hw_timer[TIMER_1].update = 0; };
  void read() { fn_timer.t0 = TIMERG1.hw_timer[TIMER_1].cnt_low; };
  void alarm_set(int s) { fn_timer.tn = fn_timer.t0 + s * TIMER_100NS_FACTOR - TIMER_ADJUST; };
  void alarm_snooze(int s) { fn_timer.tn += s * TIMER_100NS_FACTOR - TIMER_ADJUST; };
  bool test() { return (fn_timer.t0 > fn_timer.tn); };
};

extern HardwareTimer fnTimer;

#endif // FNHWTIMER_H