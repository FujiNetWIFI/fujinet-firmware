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
 * Caveat: this library used to be integrated into iwm.h/cpp with inline function definitions.
 * I tested it for 100-ns resolution in that implementation and was able to make it work
 * for SmartPort bit-banging. Since then, I switched to using SPI to create the precise
 * bit timings. I still use this library for pacing read decoding and doing timeout
 * testing on handshaking. It works fine broken out into a separate unit.
 * I have not tested this library in its new form for 100-ns accuracy and precision bit-banging.
 *
*/

#if defined(CONFIG_IDF_TARGET_ESP32)
#undef FN_USE_GPTIMER
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define FN_USE_GPTIMER
#else
#error "neither esp32 or s3"
#endif

#ifdef FN_USE_GPTIMER
#include "driver/gptimer.h"
#else /* !FN_USE_GPTIMER */
#define CONFIG_GPTIMER_SUPPRESS_DEPRECATE_WARN 1
#include "driver/timer.h"
#endif /* FN_USE_GPTIMER */

#include "soc/soc.h"
#include "soc/timer_group_struct.h"
#include "soc/clk_tree_defs.h"

// hardware timer parameters for bit-banging I/O

#ifndef FN_USE_GPTIMER
#define TIMER_DIVIDER         (2)  //  Hardware timer clock divider
#else /* FN_USE_GPTIMER */
#define TIMER_DIVIDER         (8)  //  Hardware timer clock divider
#endif /* FN_USE_GPTIMER */

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#define TIMER_SCALE           (APB_CLK_FREQ / TIMER_DIVIDER)  // convert counter value to seconds
#else
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#endif
#define TIMER_100NS_FACTOR    (TIMER_SCALE / 10000000)
#define TIMER_ADJUST          0 // substract this value to adjust for overhead

struct HardwareTimer::HideESPDetails {
#ifdef FN_USE_GPTIMER
    gptimer_handle_t gptimer;
    gptimer_config_t fn_config;
    //gptimer_alarm_config_t alarm_config;
#else /* !FN_USE_GPTIMER */
    timer_config_t fn_config;
#endif /* FN_USE_GPTIMER */
};

HardwareTimer::HardwareTimer()
{
    // configure the hardware timer for regulating bit-banging smartport i/o
    // use the idf library to get it set up
    // have own helper functions that do direct register read/write for speed

    //  typedef struct {
    //     timer_alarm_t alarm_en;         /*!< Timer alarm enable */
    //     timer_start_t counter_en;       /*!< Counter enable */
    //     timer_intr_mode_t intr_type;    /*!< Interrupt mode */
    //     timer_count_dir_t counter_dir;  /*!< Counter direction  */
    //     timer_autoreload_t auto_reload; /*!< Timer auto-reload */
    //     timer_src_clk_t clk_src;        /*!< Selects source clock. */
    //     uint32_t divider;               /*!< Counter clock divider */
    // } timer_config_t;

    espTimer = new HideESPDetails();

#ifdef FN_USE_GPTIMER
    espTimer->fn_config.clk_src = GPTIMER_CLK_SRC_APB;
    espTimer->fn_config.direction = GPTIMER_COUNT_UP;
    espTimer->fn_config.resolution_hz = 1000000;
#else /* !FN_USE_GPTIMER */
    espTimer->fn_config.alarm_en = TIMER_ALARM_DIS;
    espTimer->fn_config.counter_en = TIMER_PAUSE;
    espTimer->fn_config.intr_type = TIMER_INTR_LEVEL;
    espTimer->fn_config.counter_dir = TIMER_COUNT_UP;
    espTimer->fn_config.auto_reload = TIMER_AUTORELOAD_DIS;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    espTimer->fn_config.clk_src = TIMER_SRC_CLK_APB;
#endif
    espTimer->fn_config.divider = TIMER_DIVIDER;
    timer_init(TIMER_GROUP_1, TIMER_1, &espTimer->fn_config);
#endif /* FN_USE_GPTIMER */
}

HardwareTimer::~HardwareTimer()
{
#ifdef FN_USE_GPTIMER
    gptimer_stop(espTimer->gptimer);
    gptimer_disable(espTimer->gptimer);
    gptimer_del_timer(espTimer->gptimer);
#else /* !FN_USE_GPTIEMR */
    timer_pause(TIMER_GROUP_1, TIMER_1);
#endif /* FN_USE_GPTIMER */
    delete espTimer;
}

void HardwareTimer::config()
{
#ifdef FN_USE_GPTIMER
    gptimer_start(espTimer->gptimer);
#else /* !FN_USE_GPTIMER */
    timer_set_counter_value(TIMER_GROUP_1, TIMER_1, 0);
    timer_start(TIMER_GROUP_1, TIMER_1);
#endif /* FN_USE_GPTIMER */
}

#ifdef FN_USE_GPTIMER

void HardwareTimer::reset()
{
    gptimer_set_raw_count(espTimer->gptimer, 0);
}

void HardwareTimer::latch()
{
}

void HardwareTimer::read()
{
    uint64_t count;
    gptimer_get_raw_count(espTimer->gptimer, &count);
    fn_timer.t0 = count & 0xFFFFFFFF;
}

#else /* !FN_USE_GPTIMER */

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void HardwareTimer::reset()
{
    TIMERG1.hw_timer[TIMER_1].loadlo.val = 0;
    TIMERG1.hw_timer[TIMER_1].load.val = 0;
}

void HardwareTimer::latch()
{
    TIMERG1.hw_timer[TIMER_1].update.val = 0;
}

void HardwareTimer::read()
{
    fn_timer.t0 = TIMERG1.hw_timer[TIMER_1].lo.val;
}

#else /* ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0) */

void HardwareTimer::reset()
{
    TIMERG1.hw_timer[TIMER_1].load_low = 0;
    TIMERG1.hw_timer[TIMER_1].reload = 0;
}

void HardwareTimer::latch()
{
    TIMERG1.hw_timer[TIMER_1].update = 0;
}

void HardwareTimer::read()
{
    fn_timer.t0 = TIMERG1.hw_timer[TIMER_1].cnt_low;
}

#endif /* ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0) */

#endif /* FN_USE_GP_TIMER */

void HardwareTimer::alarm_set(int s)
{
    fn_timer.tn = fn_timer.t0 + s * TIMER_100NS_FACTOR - TIMER_ADJUST;
}

void HardwareTimer::alarm_snooze(int s)
{
    fn_timer.tn += s * TIMER_100NS_FACTOR - TIMER_ADJUST;
}

HardwareTimer fnTimer; // global object for the hardware timer
