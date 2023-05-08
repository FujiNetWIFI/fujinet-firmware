#include "led_strip.h"

#include "../../include/debug.h"

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

CRGB ledstrip[NUM_LEDS];

// Task that always runs to control led strip
void ledStripTask(void *pvParameters)
{
    bool stateChange = true; // Flag if we should update LED state
    bool rainbowRun = false; // if we are running the rainbow or not
    int rainbowMillis = 0; // keep track of time for the rainbow

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

        if (fnLedStrip.rainbowTimer > 0 && !rainbowRun)
        {
            rainbowRun = true;
            rainbowMillis = fnSystem.millis();

            rainbow_wave(25, 10);
            FastLED.show(); // make it so
        }
        else if(rainbowRun)
        {
            if ((fnSystem.millis() - rainbowMillis) > (fnLedStrip.rainbowTimer * 1000) || fnLedStrip.rainbowStopper) // Times Up!
            {
                rainbowRun = false;
                rainbowMillis = 0;
                fnLedStrip.rainbowTimer = 0;
                fnLedStrip.rainbowStopper = false;

                for (int i=0;i<LED_STRIP_COUNT;i++)
                {
                    if (fnLedStrip.sLedState[i] == LED_OFF)
                    { // Off Black
                        ledstrip[i] = CRGB::Black;
                    }
                    else if (fnLedStrip.sLedState[i] == LED_ON)
                    { // On Color
                        ledstrip[i] = fnLedStrip.sLedColor[i];
                    }
                }
            }
            else
                rainbow_wave(25, 10);

            FastLED.show();
        }
        else if (stateChange)
        {
            FastLED.clear(); // Clear the led data first
            // Prep the led states
            for (int i=0;i<LED_STRIP_COUNT;i++)
            {
                if (fnLedStrip.sLedState[i] == LED_OFF)
                { // Off Black
                    ledstrip[i] = CRGB::Black;
                }
                else if (fnLedStrip.sLedState[i] == LED_ON)
                { // On Color
                    ledstrip[i] = fnLedStrip.sLedColor[i];
                }
                else
                { // Blinking
                    // TODO: figure out how to handle this
                }
            }
            //FastLED.showColor(CRGB::Black); // Blank it first otherwise the last LED color is always wrong, WHY?
            FastLED.show(); // make it so
            stateChange = false;
        }

        vTaskDelay(10 / portTICK_PERIOD_MS); // 10ms
    }
}

void rainbow_wave(uint8_t thisSpeed, uint8_t deltaHue) {
    uint8_t thisHue = beatsin8(thisSpeed,0,255);
    //uint8_t thisHue = beat8(thisSpeed,255);

    fill_rainbow(ledstrip, NUM_LEDS, thisHue, deltaHue);
}

LedStrip::LedStrip()
{
    sLedColor[stripLed::LED_STRIP_WIFI] = CRGB::White;
    sLedColor[stripLed::LED_STRIP_BUS] = CRGB::OrangeRed;
    sLedColor[stripLed::LED_STRIP_BT] = CRGB::Blue;
}

// Setup the LED Strip
void LedStrip::setup()
{
    // Only start the strip if we found it during check_hardware_ver()
    if (fnSystem.ledstrip())
    {
        Debug_printf("Starting LED Strip\n");
        FastLED.addLeds<LED_TYPE, LED_DATA_PIN, RGB_ORDER>(ledstrip, NUM_LEDS);
        FastLED.showColor(CRGB::Black);
        FastLED.setMaxPowerInVoltsAndMilliamps(5, LED_BRIGHTNESS);

        // Start the LED Task
        xTaskCreatePinnedToCore(&ledStripTask, "LEDStripTask", 4000, NULL, 5, NULL, 0);

        // Taste the Rainbow at startup
        fnLedStrip.startRainbow(3);
    }
}

// Set the LED State
void LedStrip::set(stripLed led, bool onoff)
{
    sLedState[led] = onoff;
}

// Set the LED Color
void LedStrip::setColor(int led, CRGB color)
{
    sLedColor[led] = color;
}

// Toggle LED State
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

// Start rainbow mode
void LedStrip::startRainbow(int seconds)
{
    fnLedStrip.rainbowTimer = seconds;
}

// Stop rainbow mode
void LedStrip::stopRainbow()
{
    fnLedStrip.rainbowStopper = true;
    fnLedStrip.rainbowTimer = 0;
}
