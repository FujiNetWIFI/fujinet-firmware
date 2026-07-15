
#include "led.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnLedStrip.h"

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "esp_random.h"
#endif

#define BLINKING_TIME 100 // 200 ms

// Per-LED flicker interval (microseconds). A board's pinmap may override these;
// 0 (the default) means the LED is steady and never flickers.
#ifndef LED_WIFI_FLICKER_US
#define LED_WIFI_FLICKER_US 0
#endif
#ifndef LED_BUS_FLICKER_US
#define LED_BUS_FLICKER_US 0
#endif
#ifndef LED_BT_FLICKER_US
#define LED_BT_FLICKER_US 0
#endif

#define LED_FLICKER_TICK_US    8000   // how often the flicker timer runs
#define LED_ACTIVITY_WINDOW_US 150000 // a flickering LED keeps going this long after activity

#ifdef ESP_PLATFORM
static esp_timer_handle_t s_flickerTimer = nullptr;
#endif


// Global LED manager object
LedManager fnLedManager;

LedManager::LedManager()
{
#ifdef ESP_PLATFORM
    std::fill_n(mLedPin, eLed::LED_COUNT, GPIO_NUM_NC);
#ifdef PIN_LED_BUS
    mLedPin[eLed::LED_BUS] = PIN_LED_BUS;
#endif // PIN_LED_BUS
#ifdef PIN_LED_BT
    mLedPin[eLed::LED_BT] = PIN_LED_BT;
#endif // PIN_LED_BT
#ifdef PIN_LED_WIFI
    mLedPin[eLed::LED_WIFI] = PIN_LED_WIFI;
#endif // PIN_LED_WIFI

    mFlickerIntervalUs[eLed::LED_WIFI] = LED_WIFI_FLICKER_US;
    mFlickerIntervalUs[eLed::LED_BUS]  = LED_BUS_FLICKER_US;
    mFlickerIntervalUs[eLed::LED_BT]   = LED_BT_FLICKER_US;
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
#elif defined(PINMAP_LYNX_S3)
    fnSystem.set_pin_mode(PIN_LED_BUS, gpio_mode_t::GPIO_MODE_INPUT_OUTPUT);
    fnSystem.digital_write(PIN_LED_BUS, DIGI_HIGH);

    fnSystem.set_pin_mode(PIN_LED_BT, gpio_mode_t::GPIO_MODE_INPUT_OUTPUT);
    fnSystem.digital_write(PIN_LED_BT, DIGI_HIGH);

    fnSystem.set_pin_mode(PIN_LED_WIFI, gpio_mode_t::GPIO_MODE_INPUT_OUTPUT);
    fnSystem.digital_write(PIN_LED_WIFI, DIGI_HIGH);
#else // ! PINMAP_LYNX_S3
#ifdef PIN_LED_BUS
    if (PIN_LED_BUS != GPIO_NUM_NC)
    {
        fnSystem.set_pin_mode(PIN_LED_BUS, gpio_mode_t::GPIO_MODE_OUTPUT);
        fnSystem.digital_write(PIN_LED_BUS, DIGI_HIGH);
    }
#endif // PIN_LED_BUS

#ifdef PIN_LED_BT
    if (PIN_LED_BT != GPIO_NUM_NC)
    {
        fnSystem.set_pin_mode(PIN_LED_BT, gpio_mode_t::GPIO_MODE_OUTPUT);
        fnSystem.digital_write(PIN_LED_BT, DIGI_HIGH);
    }
#endif // PIN_LED_BUS

#ifdef PIN_LED_WIFI
    if (PIN_LED_WIFI != GPIO_NUM_NC)
    {
        fnSystem.set_pin_mode(PIN_LED_WIFI, gpio_mode_t::GPIO_MODE_OUTPUT);
        fnSystem.digital_write(PIN_LED_WIFI, DIGI_HIGH);
    }
#endif // PIN_LED_BUS
#endif // PINMAP_LYNX_S3

    // If any LED is configured to flicker, start the periodic timer that drives it
    bool needFlicker = false;
    for (int i = 0; i < eLed::LED_COUNT; i++)
        if (mFlickerIntervalUs[i] > 0)
            needFlicker = true;

    if (needFlicker)
    {
        const esp_timer_create_args_t flicker_args = {
            .callback = &LedManager::flickerTimerCb,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "led_flicker",
        };
        esp_timer_create(&flicker_args, &s_flickerTimer);
        esp_timer_start_periodic(s_flickerTimer, LED_FLICKER_TICK_US);
    }
#endif // ESP_PLATFORM
}

// Drive the physical LED - an addressable strip pixel or a plain GPIO pin
void LedManager::applyState(eLed led, bool on)
{
#ifdef ESP_PLATFORM
    if (fnSystem.ledstrip())
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
#if defined(PINMAP_A2_REV0) || defined(PINMAP_FUJIAPPLE_IEC) || defined(PINMAP_MAC_REV0)
        // FujiApple Rev 0 BUS LED has reverse logic
        if (led == LED_BUS)
            fnSystem.digital_write(mLedPin[led], (on ? DIGI_HIGH : DIGI_LOW));
        else
            fnSystem.digital_write(mLedPin[led], (on ? DIGI_LOW : DIGI_HIGH));
#else
        if (mLedPin[led] != GPIO_NUM_NC)
            fnSystem.digital_write(mLedPin[led], (on ? DIGI_LOW : DIGI_HIGH));
#endif
    }
#endif // ESP_PLATFORM
}

void LedManager::set(eLed led, bool on)
{
#ifdef ESP_PLATFORM
    if (mFlickerIntervalUs[led] > 0)
    {
        // A flickering LED is driven by the timer; set() only marks activity.
        // The 'off' edge is deliberately ignored - the timer decides off-time.
        if (on)
            mActivityUs[led] = esp_timer_get_time();
        return;
    }

    mLedState[led] = on;
    applyState(led, on);
#endif // ESP_PLATFORM
}

void LedManager::toggle(eLed led)
{
#ifdef ESP_PLATFORM
    if (mFlickerIntervalUs[led] > 0)
    {
        mActivityUs[led] = esp_timer_get_time();
        return;
    }
    set(led, !mLedState[led]);
#endif // ESP_PLATFORM
}

void LedManager::blink(eLed led, int count)
{
#ifdef ESP_PLATFORM
    for (int i = 0; i < count; i++)
    {
        toggle(led);
        fnSystem.delay(BLINKING_TIME);
        toggle(led);
        if (i < count - 1)
            fnSystem.delay(BLINKING_TIME);
    }
#endif // ESP_PLATFORM
}

#ifdef ESP_PLATFORM
void LedManager::flickerTimerCb(void *arg)
{
    static_cast<LedManager *>(arg)->flickerTick();
}

// Runs periodically: for each flicker-enabled LED, produce a fast irregular
// blink while activity is recent, then settle and restore the steady LEDs.
void LedManager::flickerTick()
{
    int64_t now = esp_timer_get_time();

    for (int i = 0; i < eLed::LED_COUNT; i++)
    {
        if (mFlickerIntervalUs[i] == 0)
            continue;

        bool active = (now - mActivityUs[i]) < LED_ACTIVITY_WINDOW_US;

        if (active)
        {
            if (now >= mNextFlickerUs[i])
            {
                mFlickerPhase[i] = !mFlickerPhase[i];
                int64_t interval = mFlickerIntervalUs[i];
                mNextFlickerUs[i] = now + interval / 2 + (esp_random() % (interval + 1));
            }
            if (mFlickerPhase[i] != mLedState[i])
            {
                mLedState[i] = mFlickerPhase[i];
                applyState((eLed)i, mFlickerPhase[i]);
            }
        }
        else if (mFlickerActive[i])
        {
            // Activity just ended: drop this LED, then restore the steady LEDs
            // (matters on boards where several logical LEDs share one pixel).
            mFlickerPhase[i] = false;
            mLedState[i] = false;
            applyState((eLed)i, false);
            for (int j = 0; j < eLed::LED_COUNT; j++)
                if (mFlickerIntervalUs[j] == 0 && mLedState[j])
                    applyState((eLed)j, true);
        }

        mFlickerActive[i] = active;
    }
}
#endif // ESP_PLATFORM
