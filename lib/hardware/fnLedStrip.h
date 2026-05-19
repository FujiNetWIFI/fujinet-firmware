#ifndef LEDSTRIP_H
#define LEDSTRIP_H

#include <cstdint>

#include "../../include/pinmap.h"

class LedStripManager
{
public:
    enum eLedID
    {
        LED_STRIP_BUS,
        LED_STRIP_BT,
        LED_STRIP_WIFI
    };

    void setup();
    void set(eLedID id, bool on);
    void toggle(eLedID id);
    void blink(eLedID led, int count);
    bool present();

private:
// simple implementation - red - Bluetooth, Blue - WiFi, Green - Bus
    unsigned char r;
    unsigned char g;
    unsigned char b;

#ifdef LED_STRIP_ACTIVITY_FLICKER
    // single status LED: white = WiFi up, fast irregular orange flicker = bus activity
    bool mWifiOn = false;
    volatile int64_t mBusActivityUs = 0;
    bool mFlickerOn = false;
    int64_t mNextFlickerUs = 0;
    int mLastShown = -1;
    void update();
    static void flickerTimerCb(void *arg);
#endif

    static const unsigned char brightness = 64;
};

// Global LedStripManager object
extern LedStripManager fnLedStrip;


#endif
