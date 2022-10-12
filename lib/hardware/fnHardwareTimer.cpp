#include "fnHardwareTimer.h"

/** how to use the fnHardwareTimer
 * 
 * questions? ask @jeffpiep
 *  
 * The fnTimer object has all the functions needed to do 100-ns level timing by polling
 * Here's kind-of how to use it:
 * 
 * First, call fnTimer.config() when setting up everything at the beginning.
 * fnTimer uses TIMER_GROUP_1, TIMER_1 so hopefully will stay out the way of anything else
 * 
 * When you want to use the timer, do the following:
 * 
 * call fnTimer.reset() somewhere near the top of the function in which you're using the timer. 
 * This will set the hardware timer back to 0. fnTimer uses the lower 32 bits of the counter.
 * The counter is running at 40 MHz. It will roll over at 107 seconds. The logic is not roll-over safe.
 * Therefore, call reset() at the start of a routine and you should be good for a bit.
 * 
 * To read the timer, it has to be latched first:
 * fnTimer.latch();        // latch highspeed timer value
 * fnTimer.read();      //  grab timer low word
 * 
 * After the timer has been latched and read, an alarm can be set:
 * fnTimer.alarm_set(1000); // logic analyzer says 40 usec
 * 
 * Then you can simply wait by polling:
 * fnTimer.wait();
 * 
 * If you want to do something while waiting, you can use the fnTimer.test() to determine if the time has passed the alarm.
 * For example, here's the basic algorithm for a timeout:
 * 
  
  fnTimer.latch();        // latch highspeed timer value
  fnTimer.read();         //  grab timer low word
  fnTimer.alarm_set(1000); // 1000 = 1000 * 100 ns = 100 us 

  while ( what ever you're waiting for that isn't happening )  
  {
    fnTimer.latch();   // latch highspeed timer value
    fnTimer.read(); // grab timer low word
    if (fnTimer.timeout())   // test for timeout
    { 
      // timeout!
    }
  };
  // if you make it to here, the event did not time out

 * 
 * Remember, you have to latch() before you read() before you set an alarm_set()
 * I made it this way in case there's some algorithm that needs the elemental functions
 * Every command matters when trying to get 100-ns level timing.
 * I found I have to compile in release mode to get that level of timing.
 * 

*/

void HardwareTimer::config()
{
  // configure the hardware timer for regulating bit-banging smartport i/o
  // use the idf library to get it set up
  // have own helper functions that do direct register read/write for speed

  timer_config_t config;
  config.divider = TIMER_DIVIDER; // default clock source is APB
  config.counter_dir = TIMER_COUNT_UP;
  config.counter_en = TIMER_PAUSE;
  config.alarm_en = TIMER_ALARM_DIS;

  timer_init(TIMER_GROUP_1, TIMER_1, &config);
  timer_set_counter_value(TIMER_GROUP_1, TIMER_1, 0);
  timer_start(TIMER_GROUP_1, TIMER_1);
}

HardwareTimer fnTimer; // global object for the hardware timer