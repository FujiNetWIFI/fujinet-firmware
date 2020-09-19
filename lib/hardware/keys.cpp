#include <cstring>

#include "fnSystem.h"
#include "fnBluetooth.h"
#include "fnWiFi.h"
#include "led.h"
#include "keys.h"
#include "sio.h"

#define LONGPRESS_TIME 1750 // 1.75 seconds to detect long press
#define SHORTPRESS_TIME 300 // 0.3 seconds to detect short press
#define TAP_MAXTIME 500 // 0.5 seconds max time between single/double tap detection

#define PIN_BUTTON_A 0
#define PIN_BUTTON_B 34
#define PIN_BUTTON_C 14

// Global KeyManager object
KeyManager fnKeyManager;

static const int mButtonPin[eKey::KEY_COUNT] = {PIN_BUTTON_A, PIN_BUTTON_B, PIN_BUTTON_C};

void KeyManager::setup()
{
    fnSystem.set_pin_mode(PIN_BUTTON_A, gpio_mode_t::GPIO_MODE_INPUT);
    fnSystem.set_pin_mode(PIN_BUTTON_B, gpio_mode_t::GPIO_MODE_INPUT);
    fnSystem.set_pin_mode(PIN_BUTTON_C, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_DOWN);

    // Start a new task to check the status of the buttons
    #define KEYS_STACKSIZE 2048
    #define KEYS_PRIORITY 1

    // Check for v1.1 board pull up and flag it if available/high
    if (fnSystem.digital_read(PIN_BUTTON_C) == DIGI_HIGH)
    {
        buttonCavail = true;
        Debug_println("FujiNet Hardware v1.1");
    }
    else
    {
        Debug_println("FujiNet Hardware v1.0");
    }

    // Disable pull down for BUTTON_C
    fnSystem.set_pin_mode(PIN_BUTTON_C, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_NONE);

    xTaskCreate(_keystate_task, "fnKeys", KEYS_STACKSIZE, this, KEYS_PRIORITY, nullptr);
}

bool KeyManager::getCAvail()
{
    return buttonCavail;
}

// Ignores the current key press
void KeyManager::ignoreKeyPress(eKey key)
{
    // A value of -1 here is our indication to ignore this key press
    _buttonActionStarted[key] = -1;
}

bool KeyManager::keyCurrentlyPressed(eKey key)
{
    return fnSystem.digital_read(mButtonPin[key]) == DIGI_LOW;
}

eKeyStatus KeyManager::getKeyStatus(eKey key)
{
    eKeyStatus result = eKeyStatus::INACTIVE;

    // Ignore requests for BUTTON_B if this seems to be a WROOM board
#ifndef BOARD_HAS_PSRAM
    if (key == BUTTON_B || key == BUTTON_C)
        return result;
#endif

    // Ignore disabled buttons
    if(_buttonDisabled[key])
        return eKeyStatus::DISABLED;

    unsigned long ms;

    // Button is PRESSED when DIGI_LOW
    if (fnSystem.digital_read(mButtonPin[key]) == DIGI_LOW)
    {
        ms = fnSystem.millis();

        // Mark this button as ACTIVE and note the time
        if (_buttonActive[key] == false)
        {
            _buttonActive[key] = true;
            _buttonActionStarted[key] = ms;
        }
        // Detect long-press when time runs out instead of waiting for release
        else
        {
            if (ms - _buttonActionStarted[key] > LONGPRESS_TIME && _buttonActionStarted[key] > 0)
            {
                result = eKeyStatus::LONG_PRESS;
                // Indicate we ignore further activity until the button is released
                _buttonActionStarted[key] = -1;
            }

        }
        
    }
    // Button is NOT pressed when DIGI_HIGH
    else
    {
        // If we'd previously marked the key as active
        if (_buttonActive[key] == true)
        {
            /* Only register an action if we logged a _buttonActionStarted value > 0
             otherwise we ignore this key press
            */
            if(_buttonActionStarted[key] > 0)
            {
                ms = fnSystem.millis();

                if (ms - _buttonActionStarted[key] > SHORTPRESS_TIME)
                    result = eKeyStatus::SHORT_PRESS;
                else
                // Anything shorter than SHORTPRESS_TIME counts as a TAP
                {
                    // Detect double-tap
                    if(_buttonLastTap[key] > 0 && (ms - _buttonLastTap[key] < TAP_MAXTIME))
                    {
                        result = eKeyStatus::DOUBLE_TAP;
                        _buttonLastTap[key] = 0;
                    }
                    else
                    {
                        result = eKeyStatus::SINGLE_TAP;
                        _buttonLastTap[key] = ms;
                    }

                }
            }
            // Since the button has been released, mark it as inactive
            _buttonActive[key] = false;
        }
    }

    return result;
}

void KeyManager::_keystate_task(void *param)
{
#ifdef BOARD_HAS_PSRAM
    #define BLUETOOTH_LED eLed::LED_BT
#else
    #define BLUETOOTH_LED eLed::LED_SIO
#endif

    KeyManager *pKM = (KeyManager *)param;

    while (true)
    {
        vTaskDelay(100 / portTICK_PERIOD_MS);

        // Check on the status of the BUTTON_A and do something useful
        switch (pKM->getKeyStatus(eKey::BUTTON_A))
        {
        case eKeyStatus::LONG_PRESS:
            Debug_println("BUTTON_A: LONG PRESS");

#ifdef BLUETOOTH_SUPPORT
            Debug_println("ACTION: Bluetooth toggle");

            if (fnBtManager.isActive())
            {
                fnBtManager.stop();
                fnLedManager.set(BLUETOOTH_LED, false);

                // Start WiFi
                fnWiFi.start();
                fnWiFi.connect();

            }
            else
            {
                // Stop WiFi
                fnWiFi.stop();

                fnLedManager.set(BLUETOOTH_LED, true); // SIO LED always ON in Bluetooth mode
                fnBtManager.start();
            }
#endif //BLUETOOTH_SUPPORT
            break;

        case eKeyStatus::SHORT_PRESS:
            Debug_println("BUTTON_A: SHORT PRESS");

            fnLedManager.blink(BLUETOOTH_LED, 2); // blink to confirm a button press

// Either toggle BT baud rate or do a disk image rotation on B_KEY SHORT PRESS
#ifdef BLUETOOTH_SUPPORT
            if (fnBtManager.isActive())
            {
                Debug_println("ACTION: Bluetooth baud rate toggle");
                fnBtManager.toggleBaudrate();
            }
            else
#endif
            {
                Debug_println("ACTION: Send image_rotate message to SIO queue");
                sio_message_t msg;
                msg.message_id = SIOMSG_DISKSWAP;
                xQueueSend(SIO.qSioMessages, &msg, 0);
            }
            break;

        case eKeyStatus::SINGLE_TAP:
            // Debug_println("BUTTON_A: SINGLE-TAP");
            break;

        case eKeyStatus::DOUBLE_TAP:
            Debug_println("BUTTON_A: DOUBLE-TAP");
            break;

        default:
            break;
        } // BUTTON_A

        if (pKM->getCAvail()){
            // Check on the status of the BUTTON_B and do something useful
            switch (pKM->getKeyStatus(eKey::BUTTON_B))
            {
            case eKeyStatus::LONG_PRESS:
                Debug_println("BUTTON_B: LONG PRESS");
                break;

            case eKeyStatus::SHORT_PRESS:
                Debug_println("BUTTON_B: SHORT PRESS");
                break;

            case eKeyStatus::SINGLE_TAP:
                // Debug_println("BUTTON_B: SINGLE-TAP");
                break;

            case eKeyStatus::DOUBLE_TAP:
                Debug_println("BUTTON_B: DOUBLE-TAP");
                fnSystem.debug_print_tasks();
                break;

            default:
                break;
            } // BUTTON_B

            // Check on the status of the BUTTON_C and do something useful
            switch (pKM->getKeyStatus(eKey::BUTTON_C))
            {
            case eKeyStatus::LONG_PRESS:
                Debug_println("BUTTON_C: LONG PRESS");
                break;

            case eKeyStatus::SHORT_PRESS:
                Debug_println("BUTTON_C: SHORT PRESS");
                Debug_println("ACTION: Reboot");
                fnSystem.reboot();
                break;

            case eKeyStatus::SINGLE_TAP:
                // Debug_println("BUTTON_C: SINGLE-TAP");
                break;

            case eKeyStatus::DOUBLE_TAP:
                Debug_println("BUTTON_C: DOUBLE-TAP");
                break;

            default:
                break;
            } // BUTTON_C
        }
        else
        {
            // Check on the status of the BUTTON_B and do something useful
            switch (pKM->getKeyStatus(eKey::BUTTON_B))
            {
            case eKeyStatus::LONG_PRESS:
                // Check if we're with a few seconds of booting and disable this button if so -
                // assume the button is stuck/disabled/non-existant
                if(fnSystem.millis() < 3000)
                {
                    Debug_println("BUTTON_B: SEEMS STUCK - DISABLING");
                    pKM->_buttonDisabled[eKey::BUTTON_B] = true;
                    break;
                }

                Debug_println("BUTTON_B: LONG PRESS");
                Debug_println("ACTION: Reboot");
                fnSystem.reboot();
                break;

            case eKeyStatus::SHORT_PRESS:
                Debug_println("BUTTON_B: SHORT PRESS");
                Debug_println("ACTION: Send debug_tape message to SIO queue");
                sio_message_t msg;
                msg.message_id = SIOMSG_DEBUG_TAPE;
                xQueueSend(SIO.qSioMessages, &msg, 0);
                break;

            case eKeyStatus::SINGLE_TAP:
                // Debug_println("BUTTON_B: SINGLE-TAP");
                break;

            case eKeyStatus::DOUBLE_TAP:
                Debug_println("BUTTON_B: DOUBLE-TAP");
                fnSystem.debug_print_tasks();
                break;

            default:
                break;
            } // BUTTON_B
        }
    }
}
