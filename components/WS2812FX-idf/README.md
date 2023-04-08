# WS2812FX pattern library

There are a few different pattern libraries scattered throughout the internet. One of the better
and more interesting started here:

https://github.com/kitesurfer1404/WS2812FX

and it uses NeoPixel, and Arduino.

This is a port to FastLED, and to ESP-IDF.

This version started from the WLED source code, for no really good reason, as of 9/11/2020.

https://github.com/Aircoookie/WLED

# differences

This port doesn't have RGBW support, nor for analog LEDs, simply because FastLED doesn't support them, so
it was easier to rip them out.

# using

See `main.cpp` for an example.

There is one interesting bit about the port, which is instead of subclassing the LEDs, 
you init it by passing the CRGB LED array in. 

This means you can support a lot of LEDs on a lot of pins. In order to code for that, make sure the CRGB LED array
you create is contiguous, like this:

```
#define NUM_LEDS1
#define NUM_LEDS2
CRGB leds[NUM_LEDS1 + NUM_LEDS2];
 FastLED.addLeds<LED_TYPE, DATA_PIN1>(&leds[0], NUM_LEDS1);
 FastLED.addLeds<LED_TYPE, DATA_PIN1>(&leds[1], NUM_LEDS2);
```

Then you can pass all of the leds array with the entire size.

Then, another benefit of this interface is the segments system. You can have a completly different mapping of
segments, arbitrarily so.

# porting and notes

Annoyingly, the old code used Arduino calls. They got urned into `#define`s.

Arduino code likes milliseconds, but ESP-IDF code likes microseconds. There should probably be some re-coding.

A benefit of the WLED version is you can reuse the WS2812FX object. Interestingly, some patterns
allocate per-pixel memory.

Probably some of the code should be made more FastLED specific, and use more of the 8-bit code.
It should also probably be integrated together with 'show' and double-buffer or similar.

