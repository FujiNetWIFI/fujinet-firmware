//
// https://github.com/Aircoookie/WLED
//


#ifndef MEATLOAF_DISPLAY_H
#define MEATLOAF_DISPLAY_H

#include "FastLED.h"
#include "FX.h"

#include "../../include/pinmap.h"
// #define NUM_LEDS 5
// #define DATA_PIN_1 27 
// #define DATA_PIN_2 14
// #define BRIGHTNESS  25
// #define LED_TYPE    WS2811
// #define COLOR_ORDER RGB



#define N_COLORS 17
static const CRGB colors[N_COLORS] = { 
  CRGB::Red,
  CRGB::Green,
  CRGB::Blue,
  CRGB::White,
  CRGB::AliceBlue,
  CRGB::ForestGreen,
  CRGB::Lavender,
  CRGB::MistyRose,
  CRGB::DarkOrchid,
  CRGB::DarkOrange,
  CRGB::Black,
  CRGB::Teal,
  CRGB::Violet,
  CRGB::Lime,
  CRGB::Chartreuse,
  CRGB::BlueViolet,
  CRGB::Aqua
};

static const char *colors_names[N_COLORS] {
  "Red",
  "Green",
  "Blue",
  "White",
  "aliceblue",
  "ForestGreen",
  "Lavender",
  "MistyRose",
  "DarkOrchid",
  "DarkOrange",
  "Black",
  "Teal",
  "Violet",
  "Lime",
  "Chartreuse",
  "BlueViolet",
  "Aqua"
};


void display_app_main();

/* test using the FX unit
**
*/

static void blinkWithFx_allpatterns(void *pvParameters);

/* test specific patterns so we know FX is working right
**
*/

typedef struct {
  const char *name;
  int   mode;
  int   secs; // secs to test it
  uint32_t color;
  int speed;
} testModes_t;


static const testModes_t testModes[] = {
  { "color wipe: all leds after each other up. Then off. Repeat. RED", FX_MODE_COLOR_WIPE, 5, 0xFF0000, 1000 },
  { "color wipe: all leds after each other up. Then off. Repeat. RGREE", FX_MODE_COLOR_WIPE, 5, 0x00FF00, 1000 },
  { "color wipe: all leds after each other up. Then off. Repeat. Blu", FX_MODE_COLOR_WIPE, 5, 0x0000FF, 1000 },
  { "chase rainbow: Color running on white.", FX_MODE_CHASE_RAINBOW, 10, 0xffffff, 200 },
  { "breath, on white.", FX_MODE_BREATH, 5, 0xffffff, 100 },
  { "breath, on red.", FX_MODE_BREATH, 5, 0xff0000, 100 },
  { "what is twinkefox? on red?", FX_MODE_TWINKLEFOX, 20, 0xff0000, 2000 },
};

#define TEST_MODES_N ( sizeof(testModes) / sizeof(testModes_t))

static void blinkWithFx_test(void *pvParameters);

static void larsonfx(void *pvParameters);
static void rainbowcyclefx(void *pvParameters);

/*
** chase sequences are good for testing correctness, because you can see
** that the colors are correct, and you can see cases where the wrong pixel is lit.
*/

#define CHASE_DELAY 200

void blinkLeds_chase2(void *pvParameters);

void ChangePalettePeriodically();

void blinkLeds_interesting(void *pvParameters);

// Going to use the ESP timer system to attempt to get a frame rate.
// According to the documentation, this is a fairly high priority,
// and one should attempt to do minimal work - such as dispatching a message to a queue.
// at first, let's try just blasting pixels on it.

// Target frames per second
#define FASTFADE_FPS 30

typedef struct {
  CHSV color;
} fastfade_t;

static void _fastfade_cb(void *param);
static void fastfade(void *pvParameters);


void blinkLeds_simple(void *pvParameters);



void blinkLeds_chase(void *pvParameters);

#endif // MEATLOAF_DISPLAY_H