#include "fnLedStrip.h"
#include "fnSystem.h"
#include "../../include/pinmap.h"

#ifdef ESP_PLATFORM
#if defined(PIN_LED_STRIP) && defined(LED_STRIP_COUNT)
#include "led_strip.h"
static led_strip_handle_t strip;
#define HAVE_LED_STRIP
#endif // defined(PIN_LED_STRIP) && defined(LED_STRIP_COUNT)
#endif // ESP_PLATFORM

#define BLINKING_TIME 100


void LedStripManager::setup()
{
    r = g = b  = 0;
#ifdef HAVE_LED_STRIP
   // LED strip general initialization, according to your led board design
    led_strip_config_t strip_config = {
        .strip_gpio_num = PIN_LED_STRIP, // The GPIO that connected to the LED strip's data line
        .max_leds = LED_STRIP_COUNT,      // The number of LEDs in the strip,
        .led_model = LED_MODEL_WS2812,        // LED strip model
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, // The color order of the strip: GRB
        .flags = {
            .invert_out = false, // don't invert the output signal
        }
    };

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,      // different clock source can lead to different power consumption
        .resolution_hz = (10 * 1000 * 1000), // RMT counter clock frequency - 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
        .mem_block_symbols = 0,              // let the driver choose a proper memory block size automatically
        .flags = {
            .with_dma = 0,     // Using DMA can improve performance when driving more LEDs
        }
    };

    // LED Strip object handle
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &strip));

    for(int i = 0; i < LED_STRIP_COUNT; i++)
        led_strip_set_pixel(strip, i, r, g, b);
    
    led_strip_refresh(strip);
#endif
}

void LedStripManager::set(eLedID id, bool on)
{
#ifdef HAVE_LED_STRIP
    switch(id)
    {
    case LED_STRIP_BUS:
        g = on?brightness:0;
        break;
    case LED_STRIP_BT:
        r = on?brightness:0;
        break;
    case LED_STRIP_WIFI:
        b = on?brightness:0;
        break;
    };
    for(int i = 0; i < LED_STRIP_COUNT; i++)
        led_strip_set_pixel(strip, i, r, g, b);
    
    led_strip_refresh(strip);
#endif
}

void LedStripManager::toggle(eLedID id)
{
#ifdef HAVE_LED_STRIP
    switch(id)
    {
    case LED_STRIP_BUS:
        g = brightness-g;
        break;
    case LED_STRIP_BT:
        r = brightness-r;
        break;
    case LED_STRIP_WIFI:
        b = brightness-b;
        break;
    };
    for(int i = 0; i < LED_STRIP_COUNT; i++)
        led_strip_set_pixel(strip, i, r, g, b);
    
    led_strip_refresh(strip);
#endif
}

void LedStripManager::blink(eLedID led, int count)
{
    for(int i = 0; i < count; i++)
    {
        toggle(led);
        fnSystem.delay(BLINKING_TIME);
        toggle(led);
        if(i < count - 1)
            fnSystem.delay(BLINKING_TIME);
    }
}

bool LedStripManager::present()
{
#ifdef HAVE_LED_STRIP
    return true;
#else
    return false;
#endif
}

LedStripManager fnLedStrip;