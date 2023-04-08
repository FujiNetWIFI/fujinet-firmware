/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#ifdef LED_STRIP


#include "display.h"

#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"

#include "FastLED.h"
#include "FX.h"


CRGB leds1[NUM_LEDS];
CRGB leds2[NUM_LEDS];

CRGBPalette16 currentPalette;
TBlendType    currentBlending;

#include "palettes.h"

extern CRGBPalette16 myRedWhiteBluePalette;
extern const TProgmemPalette16 IRAM_ATTR myRedWhiteBluePalette_p;


void display_app_main() {
  printf(" entering app main, call add leds\n");
  // the WS2811 family uses the RMT driver
  FastLED.addLeds<LED_TYPE, DATA_PIN_1>(leds1, NUM_LEDS);
  //FastLED.addLeds<LED_TYPE, DATA_PIN_2>(leds2, NUM_LEDS);

  FastLED.showColor(BLACK);

  // this is a good test because it uses the GPIO ports, these are 4 wire not 3 wire
  //FastLED.addLeds<APA102, 13, 15>(leds, NUM_LEDS);

  printf(" set max power\n");
  // I have a 2A power supply, although it's 12v
  FastLED.setMaxPowerInVoltsAndMilliamps(5,200);

  // change the task below to one of the functions above to try different patterns
  printf("create task for led blinking\n");

  //xTaskCreatePinnedToCore(&blinkLeds_simple, "blinkLeds", 4000, NULL, 5, NULL, 0);
  //xTaskCreatePinnedToCore(&fastfade, "blinkLeds", 4000, NULL, 5, NULL, 0);
  //xTaskCreatePinnedToCore(&blinkWithFx_allpatterns, "blinkLeds", 4000, NULL, 5, NULL, 0);
  //xTaskCreatePinnedToCore(&blinkWithFx_test, "blinkLeds", 4000, NULL, 5, NULL, 0);
  //xTaskCreatePinnedToCore(&blinkLeds_chase, "blinkLeds", 4000, NULL, 5, NULL, 0);
  //xTaskCreatePinnedToCore(&blinkLeds_chase2, "blinkLeds", 4000, NULL, 5, NULL, 0);
  //xTaskCreatePinnedToCore(&larsonfx, "larsonscanner", 4000, NULL, 5, NULL, 0);
  xTaskCreatePinnedToCore(&rainbowcyclefx, "rainbowcycle", 4000, NULL, 5, NULL, 0);
}

/* Larson scanner
**
*/

static void larsonfx(void *pvParameters) {

	WS2812FX ws2812fx;
  WS2812FX::Segment *segments = ws2812fx.getSegments();

	// ws2812fx.init(NUM_LEDS, leds1, false); // type was configured before
	// ws2812fx.setBrightness(BRIGHTNESS);
	// ws2812fx.setMode(0 /*segid*/, FX_MODE_LARSON_SCANNER);

  // //segments[0].colors[1] = 255U; //blue, white 16777215U??
  // segments[0].colors[0] = 0xFF0000; //CRGB::RED; //red?
  // segments[0].speed = 128;

  ws2812fx.init(NUM_LEDS, leds1, false);
  ws2812fx.setBrightness(BRIGHTNESS);
  ws2812fx.setMode(0 /*segid*/, FX_MODE_LARSON_SCANNER);
  
  segments[0].colors[0] = 0xFF0000; // RED
  segments[0].colors[1] = 0x00FF00; // BLACK
  segments[0].colors[2] = 0x0000FF; // BLACK
  segments[0].speed = 255;

  while (true)
  {
		ws2812fx.service();
		vTaskDelay(10 / portTICK_PERIOD_MS); /*10ms*/
  }
};

static void rainbowcyclefx(void *pvParameters) {

	WS2812FX ws2812fx;
  WS2812FX::Segment *segments = ws2812fx.getSegments();

  ws2812fx.init(NUM_LEDS, leds1, false);
  ws2812fx.setBrightness(BRIGHTNESS);
  ws2812fx.setMode(0, FX_MODE_THEATER_CHASE_RAINBOW);
  segments[0].speed = 128;

  while (true)
  {
		ws2812fx.service();
		vTaskDelay(10 / portTICK_PERIOD_MS); /*10ms*/
  }
};

/* test using the FX unit
**
*/

static void blinkWithFx_allpatterns(void *pvParameters) {

	uint16_t mode = FX_MODE_STATIC;

	WS2812FX ws2812fx;

	ws2812fx.init(NUM_LEDS, leds1, false); // type was configured before
	ws2812fx.setBrightness(BRIGHTNESS);
	ws2812fx.setMode(0 /*segid*/, mode);


	// microseconds
	uint64_t mode_change_time = esp_timer_get_time();

	while (true) {

		if ((mode_change_time + 10000000L) < esp_timer_get_time() ) {
			mode += 1;
			mode %= MODE_COUNT;
			mode_change_time = esp_timer_get_time();
			ws2812fx.setMode(0 /*segid*/, mode);
			printf(" changed mode to %d\n", mode);
		}

		ws2812fx.service();
		vTaskDelay(10 / portTICK_PERIOD_MS); /*10ms*/
	}
};

/* test specific patterns so we know FX is working right
**
*/


#define TEST_MODES_N ( sizeof(testModes) / sizeof(testModes_t))

static void blinkWithFx_test(void *pvParameters) {

  WS2812FX ws2812fx;
  WS2812FX::Segment *segments = ws2812fx.getSegments();

  ws2812fx.init(NUM_LEDS, leds1, false); // type was configured before
  ws2812fx.setBrightness(255);

  int test_id = 0;
  printf(" start mode: %s\n",testModes[test_id].name);
  ws2812fx.setMode(0 /*segid*/, testModes[test_id].mode);
  segments[0].colors[0] = testModes[test_id].color;
  segments[0].speed = testModes[test_id].speed;
  uint64_t nextMode = esp_timer_get_time() + (testModes[test_id].secs * 1000000L );


  while (true) {

    uint64_t now = esp_timer_get_time();

    if (nextMode < now ) {
      test_id = (test_id +1) % TEST_MODES_N;
      nextMode = esp_timer_get_time() + (testModes[test_id].secs * 1000000L );
      ws2812fx.setMode(0 /*segid*/, testModes[test_id].mode);
      segments[0].colors[0] = testModes[test_id].color;
      segments[0].speed = testModes[test_id].speed;
      printf(" changed mode to: %s\n",testModes[test_id].name);
    }

    ws2812fx.service();
    vTaskDelay(10 / portTICK_PERIOD_MS); /*10ms*/
  }
};


/*
** chase sequences are good for testing correctness, because you can see
** that the colors are correct, and you can see cases where the wrong pixel is lit.
*/

#define CHASE_DELAY 200

void blinkLeds_chase2(void *pvParameters) {

  while(true) {

    for (int ci = 0; ci < N_COLORS; ci++) {
      CRGB color = colors[ci];
      printf(" chase: *** color %s ***\n",colors_names[ci]);

      // set strings to black first
      fill_solid(leds1, NUM_LEDS, CRGB::Black);
      fill_solid(leds2, NUM_LEDS, CRGB::Black);
      FastLED.show();

      int prev;

      // forward
      printf(" chase: forward\n");
      prev = -1;
      for (int i = 0; i < NUM_LEDS; i++) {
        if (prev >= 0) {
          leds2[prev] = leds1[prev] = CRGB::Black;
        }
        leds2[i] = leds1[i] = color;
        prev = i;

        FastLED.show();
        delay(CHASE_DELAY);
      }

      printf(" chase: backward\n");
      prev = -1;
      for (int i = NUM_LEDS-1; i >= 0; i--) {
        if (prev >= 0) {
          leds2[prev] = leds1[prev] = CRGB::Black;
        }
        leds2[i] = leds1[i] = color;
        prev = i;

        FastLED.show();
        delay(CHASE_DELAY);
      }

      // two at a time
      printf(" chase: twofer\n");
      prev = -1;
      for (int i = 0; i < NUM_LEDS; i += 2) {
        if (prev >= 0) {
          leds2[prev] = leds1[prev] = CRGB::Black;
          leds2[prev+1] = leds1[prev+1] = CRGB::Black;
        }
        leds2[i] = leds1[i] = color;
        leds2[i+1] = leds1[i+1] = color;
        prev = i;

        FastLED.show();
        delay(CHASE_DELAY);
      }

    } // for all colors
  } // while true

}

void ChangePalettePeriodically(){

  uint8_t secondHand = (millis() / 1000) % 60;
  static uint8_t lastSecond = 99;

  if( lastSecond != secondHand) {
    lastSecond = secondHand;
    if( secondHand ==  0)  { currentPalette = RainbowColors_p;         currentBlending = LINEARBLEND; }
    if( secondHand == 10)  { currentPalette = RainbowStripeColors_p;   currentBlending = NOBLEND;  }
    if( secondHand == 15)  { currentPalette = RainbowStripeColors_p;   currentBlending = LINEARBLEND; }
    if( secondHand == 20)  { SetupPurpleAndGreenPalette();             currentBlending = LINEARBLEND; }
    if( secondHand == 25)  { SetupTotallyRandomPalette();              currentBlending = LINEARBLEND; }
    if( secondHand == 30)  { SetupBlackAndWhiteStripedPalette();       currentBlending = NOBLEND; }
    if( secondHand == 35)  { SetupBlackAndWhiteStripedPalette();       currentBlending = LINEARBLEND; }
    if( secondHand == 40)  { currentPalette = CloudColors_p;           currentBlending = LINEARBLEND; }
    if( secondHand == 45)  { currentPalette = PartyColors_p;           currentBlending = LINEARBLEND; }
    if( secondHand == 50)  { currentPalette = myRedWhiteBluePalette_p; currentBlending = NOBLEND;  }
    if( secondHand == 55)  { currentPalette = myRedWhiteBluePalette_p; currentBlending = LINEARBLEND; }
  }

}

void blinkLeds_interesting(void *pvParameters){
  while(1){
  	printf("blink leds\n");
    ChangePalettePeriodically();
    
    static uint8_t startIndex = 0;
    startIndex = startIndex + 1; /* motion speed */
    
    for( int i = 0; i < NUM_LEDS; i++) {
        leds1[i] = ColorFromPalette( currentPalette, startIndex, 64, currentBlending);
        leds2[i] = ColorFromPalette( currentPalette, startIndex, 64, currentBlending);
        startIndex += 3;
    }
    printf("show leds\n");
    FastLED.show();
    delay(400);
  };

};

// Going to use the ESP timer system to attempt to get a frame rate.
// According to the documentation, this is a fairly high priority,
// and one should attempt to do minimal work - such as dispatching a message to a queue.
// at first, let's try just blasting pixels on it.

// Target frames per second
#define FASTFADE_FPS 30

static void _fastfade_cb(void *param){

  fastfade_t *ff = (fastfade_t *)param;

  ff->color.hue++;

  if (ff->color.hue % 10 == 0) {
    printf("fast hsv fade h: %d s: %d v: %d\n",ff->color.hue,ff->color.s, ff->color.v);
  }

  fill_solid(leds1,NUM_LEDS,ff->color);
  fill_solid(leds2,NUM_LEDS,ff->color);

  FastLED.show();

};


static void fastfade(void *pvParameters){

  fastfade_t ff_t = {
    .color = CHSV(0/*hue*/,255/*sat*/,255/*value*/)
  };

  esp_timer_create_args_t timer_create_args = {
        .callback = _fastfade_cb,
        .arg = (void *) &ff_t,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "fastfade_timer"
    };

  esp_timer_handle_t timer_h;

  esp_timer_create(&timer_create_args, &timer_h);

  esp_timer_start_periodic(timer_h, 1000000L / FASTFADE_FPS );

  // suck- just trying this
  while(1){

      vTaskDelay(1000 / portTICK_PERIOD_MS);
  };

}



void blinkLeds_simple(void *pvParameters){

 	while(1){

		for (int j=0;j<N_COLORS;j++) {
			printf("blink leds\n");

			for (int i=0;i<NUM_LEDS;i++) {
			  leds1[i] = colors[j];
        leds2[i] = colors[j];
			}
			FastLED.show();
			delay(1000);
		};
	}
};

#define N_COLORS_CHASE 7
CRGB colors_chase[N_COLORS_CHASE] = { 
  CRGB::AliceBlue,
  CRGB::Lavender,
  CRGB::DarkOrange,
  CRGB::Red,
  CRGB::Green,
  CRGB::Blue,
  CRGB::White,
};

void blinkLeds_chase(void *pvParameters) {
  int pos = 0;
  int led_color = 0;
  while(1){
  	printf("chase leds\n");

  		// do it the dumb way - blank the leds
	    for (int i=0;i<NUM_LEDS;i++) {
	      leds1[i] =   CRGB::Black;
        leds2[i] =   CRGB::Black;
	    }

	    // set the one LED to the right color
	    leds1[pos] = leds2[pos] = colors_chase[led_color];
	    pos = (pos + 1) % NUM_LEDS;

	    // use a new color
	    if (pos == 0) {
	    	led_color = (led_color + 1) % N_COLORS_CHASE ;
	    }

	    uint64_t start = esp_timer_get_time();
	    FastLED.show();
	    uint64_t end = esp_timer_get_time();
	    printf("Show Time: %" PRIu64 "\n",end-start);
	    delay(200);
	 };

}


#endif // #ifdef LED_STRIP