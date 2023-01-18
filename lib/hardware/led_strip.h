#ifndef LED_STRIP_H
#define LED_STIP_H

#include "FastLED.h"
//#include "FX.h"

#include "../../include/pinmap.h"

#define LED_BLINK_TIME 100

enum stripLed
{
  LED_STRIP_WIFI = LED_WIFI_NUM,
  LED_STRIP_BUS = LED_BUS_NUM,
  LED_STRIP_BT = LED_BT_NUM,
  LED_STRIP_COUNT = NUM_LEDS
};

enum ledState
{
  LED_OFF = 0,
  LED_ON = 1,
  LED_BLINK = 2
};

// Task that always runs to control led strip
void ledStripTask(void *pvParameters);

class LedStrip
{
public:
  int sLedState[NUM_LEDS] = { LED_OFF }; // default off
  CRGB sLedColor[NUM_LEDS] = { CRGB::Black }; // default black (off)
  int sLedBlinkCount[NUM_LEDS] = { 0 }; // default no blinky
  CRGB ledstrip[NUM_LEDS];

  LedStrip();
  void setup();
  void set(stripLed led, bool onoff);
  void setColor(int led, CRGB color);
  void toggle(stripLed led);
  void blink(stripLed led, int count=1);

private:
};

extern LedStrip fnLedStrip;
#endif // LED_STRIP_H