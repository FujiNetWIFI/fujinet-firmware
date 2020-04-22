#include <Arduino.h>
#include "led.h"

#if defined(ESP8266)
#define PIN_LED_WIFI 2
#define PIN_LED_SIO 2
#define PIN_LED_BT 2
#elif defined(ESP32)
#define PIN_LED_WIFI 2
#define PIN_LED_SIO 4

// pins 12-15 are used to interface with the JTAG debugger
// so leave them alone if we're using JTAG
#ifndef JTAG 
#define PIN_LED_BT 13
#else
#define PIN_LED_BT 4
#endif

#endif

LedManager::LedManager()
{
    memset(mLedState, false, sizeof(bool) * eLed::LED_COUNT);
    mLedPin[eLed::LED_WIFI] = PIN_LED_WIFI;
    mLedPin[eLed::LED_SIO] = PIN_LED_SIO;
    mLedPin[eLed::LED_BT] = PIN_LED_BT;
}

void LedManager::setup()
{
    pinMode(PIN_LED_WIFI, OUTPUT);
    digitalWrite(PIN_LED_WIFI, HIGH); // OFF
 
    pinMode(PIN_LED_SIO, OUTPUT);
    digitalWrite(PIN_LED_SIO, HIGH); // OFF
 
    pinMode(PIN_LED_BT, OUTPUT);
    digitalWrite(PIN_LED_BT, HIGH); // OFF
}

void LedManager::set(eLed led, bool on)
{
    mLedState[led] = on;
    digitalWrite(mLedPin[led], (on ? LOW : HIGH));
}

void LedManager::toggle(eLed led)
{
    if(mLedState[led])
    {
        set(led, false);
    }
    else
    {
        set(led, true);
    }
}

void LedManager::blink(eLed led)
{
    ledMgr.toggle(led);
    delay(BLINKING_TIME);
    ledMgr.toggle(led);
}