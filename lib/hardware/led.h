#ifndef LED_H
#define LED_H

#include <cstdint>

#include "../../include/pinmap.h"

enum eLed
{
    LED_WIFI = 0,
    LED_BUS,
    LED_BT,
    LED_COUNT
};

class LedManager
{
public:
    LedManager();
    void setup();
    void set(eLed led, bool one=true);
    void toggle(eLed led);
    void blink(eLed led, int count=1);

private:
    void applyState(eLed led, bool on);
#ifdef ESP_PLATFORM
    void flickerTick();
    static void flickerTimerCb(void *arg);
#endif

    bool mLedState[eLed::LED_COUNT] = { 0 };
    int mLedPin[eLed::LED_COUNT];

    // Activity-driven flicker (e.g. a hard-drive style bus LED). A board's pinmap
    // sets the interval; mFlickerIntervalUs[led] == 0 means that LED never flickers.
    int64_t mFlickerIntervalUs[eLed::LED_COUNT] = { 0 };
    int64_t mActivityUs[eLed::LED_COUNT] = { 0 };
    int64_t mNextFlickerUs[eLed::LED_COUNT] = { 0 };
    bool mFlickerPhase[eLed::LED_COUNT] = { 0 };
    bool mFlickerActive[eLed::LED_COUNT] = { 0 };
};

extern LedManager fnLedManager;
#endif // guard
