#include <Arduino.h>
#include "led.h"

#if defined(ESP8266)
#define PIN_LED_WIFI 2
#define PIN_LED_SIO 2
#define PIN_LED3 2
#elif defined(ESP32)
#define PIN_LED_WIFI 2
#define PIN_LED_SIO 4
#define PIN_LED3 13
#endif

LedManager::LedManager()
{
    memset(mLedState, false, sizeof(bool) * eLed::LED_COUNT);
    mLedPin[eLed::LED_WIFI] = PIN_LED_WIFI;
    mLedPin[eLed::LED_SIO] = PIN_LED_SIO;
    mLedPin[eLed::LED3] = PIN_LED3;
}

void LedManager::setup()
{
    pinMode(PIN_LED_WIFI, OUTPUT);
    digitalWrite(PIN_LED_WIFI, HIGH); // OFF
 
    pinMode(PIN_LED_SIO, OUTPUT);
    digitalWrite(PIN_LED_SIO, HIGH); // OFF
 
    pinMode(PIN_LED3, OUTPUT);
    digitalWrite(PIN_LED3, HIGH); // OFF
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