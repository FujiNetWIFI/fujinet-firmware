#ifndef FNHWTIMER_H
#define FNHWTIMER_H

#include "esp_idf_version.h"
#include "sdkconfig.h"
#include <stdint.h>

class HardwareTimer
{
private:
    struct HideESPDetails;
    HideESPDetails *espTimer;
    struct fn_timer_t
    {
        uint32_t tn;
        uint32_t t0;
    } fn_timer;

public:
    HardwareTimer();
    ~HardwareTimer();

    void config();

    void reset();
    void latch();
    void read();

    bool timeout() { return (fn_timer.t0 > fn_timer.tn); };
    void wait() { do{latch(); read();} while (!timeout()); };
    void alarm_set(int s);
    void alarm_snooze(int s);
};

extern HardwareTimer fnTimer;

#endif // FNHWTIMER_H
