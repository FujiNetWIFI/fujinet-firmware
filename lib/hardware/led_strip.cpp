#ifdef LED_STRIP

#include "led_strip.h"

#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"

#include "fnSystem.h"

#include "FastLED.h"
//#include "FX.h"

// Global LED manager object
LedStrip fnLedStrip;

// Task that always runs to control led strip
void ledStripTask(void *pvParameters)
{
    bool stateChange = true; // Flag if we should update LED state

    // Get current LED states
    int wifiLedState = fnLedStrip.sLedState[LED_STRIP_WIFI];
    int busLedState = fnLedStrip.sLedState[LED_STRIP_BUS];
    int btLedState = fnLedStrip.sLedState[LED_STRIP_BT];

    while (true)
    {
        // Check and set LED mode changes
        if (wifiLedState != fnLedStrip.sLedState[LED_STRIP_WIFI])
        {
            wifiLedState = fnLedStrip.sLedState[LED_STRIP_WIFI]; // set new state
            stateChange = true;
        }
        if (busLedState != fnLedStrip.sLedState[LED_STRIP_BUS])
        {
            busLedState = fnLedStrip.sLedState[LED_STRIP_BUS]; // set new state
           stateChange = true;
        }
        if (btLedState != fnLedStrip.sLedState[LED_STRIP_BT])
        {
            btLedState = fnLedStrip.sLedState[LED_STRIP_BT]; // set new state
            stateChange = true;
        }

        if (stateChange)
        {
            // Prep the led states
            for (int i=0;i<LED_STRIP_COUNT;i++)
            {
                if (fnLedStrip.sLedState[i] == LED_OFF)
                { // Off Black
                    fnLedStrip.ledstrip[i] = CRGB::Black;
                }
                else if (fnLedStrip.sLedState[i] == LED_ON)
                { // On Color
                    fnLedStrip.ledstrip[i] = fnLedStrip.sLedColor[i];
                }
                else
                { // Blinking
                    // TODO: figure out how to handle this
                }
            }

            FastLED.showColor(CRGB::Black); // Blank it first otherwise the last LED color is always wrong, WHY?
            FastLED.show(); // make it so
            stateChange = false;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS); /*10ms*/
    }
}

LedStrip::LedStrip()
{
    sLedColor[stripLed::LED_STRIP_WIFI] = CRGB::White;
    sLedColor[stripLed::LED_STRIP_BUS] = CRGB::Orange;
    sLedColor[stripLed::LED_STRIP_BT] = CRGB::Blue;
}

// Setup the LED Strip
void LedStrip::setup()
{
    FastLED.addLeds<LED_TYPE, LED_DATA_PIN, RGB_ORDER>(ledstrip, NUM_LEDS);
    FastLED.showColor(CRGB::Black);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, LED_BRIGHTNESS);

    // Start the LED Task
    xTaskCreatePinnedToCore(&ledStripTask, "LEDStripTask", 4000, NULL, 5, NULL, 0);
}

// Set the LED Mode
void LedStrip::set(stripLed led, bool onoff)
{
    sLedState[led] = onoff;
}

// Set the LED Color
void LedStrip::setColor(int led, CRGB color)
{
    sLedColor[led] = color;
}

// Toggle LED Mode
void LedStrip::toggle(stripLed led)
{
    // If the LED is off, turn it on
    if (sLedState[led] == LED_OFF)
        sLedState[led] = LED_ON;
    // If blinking or on, turn it off
    else
        sLedState[led] = LED_OFF;
}

// Set blink mode
void LedStrip::blink(stripLed led, int count)
{
    sLedBlinkCount[led] = count;
    sLedState[led] = LED_BLINK;
}

#endif // LED_STRIP