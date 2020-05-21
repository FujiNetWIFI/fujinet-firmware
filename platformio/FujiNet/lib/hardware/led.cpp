//#include <Arduino.h>
#include <cstring>
#include "led.h"
#include "fnSystem.h"

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
    //memset(mLedState, false, sizeof(bool) * eLed::LED_COUNT);
    mLedPin[eLed::LED_WIFI] = PIN_LED_WIFI;
    mLedPin[eLed::LED_SIO] = PIN_LED_SIO;
    mLedPin[eLed::LED_BT] = PIN_LED_BT;
}

void LedManager::setup()
{
    //pinMode(PIN_LED_WIFI, OUTPUT);
    //digitalWrite(PIN_LED_WIFI, HIGH); // OFF
    fnSystem.set_pin_mode(PIN_LED_WIFI, PINMODE_OUTPUT);
    fnSystem.digital_write(PIN_LED_WIFI, DIGI_HIGH);
 
    //pinMode(PIN_LED_SIO, OUTPUT);
    //digitalWrite(PIN_LED_SIO, HIGH); // OFF
    fnSystem.set_pin_mode(PIN_LED_SIO, PINMODE_OUTPUT);
    fnSystem.digital_write(PIN_LED_SIO, DIGI_HIGH);
 
    //pinMode(PIN_LED_BT, OUTPUT);
    //digitalWrite(PIN_LED_BT, HIGH); // OFF
    fnSystem.set_pin_mode(PIN_LED_BT, PINMODE_OUTPUT);
    fnSystem.digital_write(PIN_LED_BT, DIGI_HIGH);    
}

void LedManager::set(eLed led, bool on)
{
    mLedState[led] = on;
    // digitalWrite(mLedPin[led], (on ? LOW : HIGH));
    fnSystem.digital_write(mLedPin[led], (on ? DIGI_LOW : DIGI_HIGH));
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
    //delay(BLINKING_TIME);
    fnSystem.delay(BLINKING_TIME);
    ledMgr.toggle(led);
}
