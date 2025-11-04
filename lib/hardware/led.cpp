
#include "led.h"

#include "fnSystem.h"
#include "fnLedStrip.h"

#define BLINKING_TIME 100 // 200 ms


// Global LED manager object
LedManager fnLedManager;

LedManager::LedManager()
{
#ifdef ESP_PLATFORM
    mLedPin[eLed::LED_BUS] = PIN_LED_BUS;
    mLedPin[eLed::LED_BT] = PIN_LED_BT;
    mLedPin[eLed::LED_WIFI] = PIN_LED_WIFI;
#endif
}

// Sets required pins to OUTPUT mode and makes sure they're initially off
void LedManager::setup()
{
#ifdef ESP_PLATFORM
#if defined(PINMAP_A2_REV0) || defined(PINMAP_FUJIAPPLE_IEC) || defined(PINMAP_MAC_REV0)
    fnSystem.set_pin_mode(PIN_LED_BUS, gpio_mode_t::GPIO_MODE_OUTPUT);
    fnSystem.digital_write(PIN_LED_BUS, DIGI_LOW);

    fnSystem.set_pin_mode(PIN_LED_WIFI, gpio_mode_t::GPIO_MODE_OUTPUT);
    fnSystem.digital_write(PIN_LED_WIFI, DIGI_HIGH);
#elif defined(PINMAP_RS232_REV0)
    fnSystem.set_pin_mode(PIN_LED_BUS, gpio_mode_t::GPIO_MODE_INPUT_OUTPUT);
    fnSystem.digital_write(PIN_LED_BUS, DIGI_HIGH);

    fnSystem.set_pin_mode(PIN_LED_WIFI, gpio_mode_t::GPIO_MODE_INPUT_OUTPUT);
    fnSystem.digital_write(PIN_LED_WIFI, DIGI_HIGH);
#else
    fnSystem.set_pin_mode(PIN_LED_BUS, gpio_mode_t::GPIO_MODE_OUTPUT);
    fnSystem.digital_write(PIN_LED_BUS, DIGI_HIGH);

    fnSystem.set_pin_mode(PIN_LED_BT, gpio_mode_t::GPIO_MODE_OUTPUT);
    fnSystem.digital_write(PIN_LED_BT, DIGI_HIGH);

    fnSystem.set_pin_mode(PIN_LED_WIFI, gpio_mode_t::GPIO_MODE_OUTPUT);
    fnSystem.digital_write(PIN_LED_WIFI, DIGI_HIGH);
#endif
#endif // ESP_PLATFORM
}

void LedManager::set(eLed led, bool on)
{
#ifdef ESP_PLATFORM
    if(fnSystem.ledstrip())
    {
        switch (led)
        {
        case eLed::LED_BUS:
            fnLedStrip.set(LedStripManager::LED_STRIP_BUS, on);
            break;
        case eLed::LED_BT:
            fnLedStrip.set(LedStripManager::LED_STRIP_BT, on);
            break;
        case eLed::LED_WIFI:
            fnLedStrip.set(LedStripManager::LED_STRIP_WIFI, on);
            break;
        default:
            break;
        }
    }
    else
    {
        mLedState[led] = on;
#if defined(PINMAP_A2_REV0) || defined(PINMAP_FUJIAPPLE_IEC) || defined(PINMAP_MAC_REV0)
        // FujiApple Rev 0 BUS LED has reverse logic
        if (led == LED_BUS)
            fnSystem.digital_write(mLedPin[led], (on ? DIGI_HIGH : DIGI_LOW));
        else
            fnSystem.digital_write(mLedPin[led], (on ? DIGI_LOW : DIGI_HIGH));
#else
        fnSystem.digital_write(mLedPin[led], (on ? DIGI_LOW : DIGI_HIGH));
#endif
    }
#endif // ESP_PLATFORM
}

void LedManager::toggle(eLed led)
{
#ifdef ESP_PLATFORM
    if(fnSystem.ledstrip())
    {
        switch (led)
        {
        case eLed::LED_BUS:
            fnLedStrip.toggle(LedStripManager::LED_STRIP_BUS);
            break;
        case eLed::LED_BT:
            fnLedStrip.toggle(LedStripManager::LED_STRIP_BT);
            break;
        case eLed::LED_WIFI:
            fnLedStrip.toggle(LedStripManager::LED_STRIP_WIFI);
            break;
        default:
            break;
        }
    }
    else
    {
        set(led, !mLedState[led]);
    }
#endif // ESP_PLATFORM
}

void LedManager::blink(eLed led, int count)
{
#ifdef ESP_PLATFORM
    if(fnSystem.ledstrip())
    {
        switch (led)
        {
        case eLed::LED_BUS:
            fnLedStrip.blink(LedStripManager::LED_STRIP_BUS, count);
            break;
        case eLed::LED_BT:
            fnLedStrip.blink(LedStripManager::LED_STRIP_BT, count);
            break;
        case eLed::LED_WIFI:
            fnLedStrip.blink(LedStripManager::LED_STRIP_WIFI, count);
            break;
        default:
            break;
        }
    }
    else
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
#endif // ESP_PLATFORM
}
