#include <cstring>

#include "fnSystem.h"
#include "fnBluetooth.h"
#include "fnWiFi.h"
#include "led.h"
#include "keys.h"
#include "bus.h"
#include "fnConfig.h"

#include "../../include/pinmap.h"

#define LONGPRESS_TIME 1500 // 1.5 seconds to detect long press
#define DOUBLETAP_DETECT_TIME 400 // ms to wait to see if it's a single/double tap


#define IGNORE_KEY_EVENT -1

// Global KeyManager object
KeyManager fnKeyManager;

static const int mButtonPin[eKey::KEY_COUNT] = {PIN_BUTTON_A, PIN_BUTTON_B, PIN_BUTTON_C};

void KeyManager::setup()
{
    fnSystem.set_pin_mode(PIN_BUTTON_A, gpio_mode_t::GPIO_MODE_INPUT);
    fnSystem.set_pin_mode(PIN_BUTTON_B, gpio_mode_t::GPIO_MODE_INPUT);

    // Enable safe reset on Button C if available
    if (fnSystem.get_hardware_ver() >= 2)
    {
        has_button_c = true;
        fnSystem.set_pin_mode(PIN_BUTTON_C, gpio_mode_t::GPIO_MODE_INPUT, SystemManager::pull_updown_t::PULL_NONE);
        Debug_println("Enabled Safe Reset Button C");
    }

    // Start a new task to check the status of the buttons
    #define KEYS_STACKSIZE 4096
    #define KEYS_PRIORITY 1
    xTaskCreate(_keystate_task, "fnKeys", KEYS_STACKSIZE, this, KEYS_PRIORITY, nullptr);
}


// Ignores the current key press
void KeyManager::ignoreKeyPress(eKey key)
{
    _keys[key].action_started_ms = IGNORE_KEY_EVENT;
}

bool KeyManager::keyCurrentlyPressed(eKey key)
{
    return fnSystem.digital_read(mButtonPin[key]) == DIGI_LOW;
}

/* 
There are 3 types of actions we're looking for:
   * LONG_PRESS: User holds button for at least LONGPRESS_TIME
   * SHORT_PRESS: User presses button and releases in less than LONGPRESS_TIME,
   *              and there isn't another event for DOUBLETAP_DETECT_TIME
   * DOUBLE_TAP: User presses button and releases in less than LONGPRESS_TIME,
   *             and the last SHORT_PRESS was within DOUBLETAP_DETECT_TIME
*/
eKeyStatus KeyManager::getKeyStatus(eKey key)
{
    eKeyStatus result = eKeyStatus::INACTIVE;

    // Ignore disabled buttons
    if(_keys[key].disabled)
        return eKeyStatus::DISABLED;

    unsigned long ms = fnSystem.millis();

    // Button is PRESSED when DIGI_LOW
    if (fnSystem.digital_read(mButtonPin[key]) == DIGI_LOW)
    {
        // If not already active, mark as ACTIVE and note the time
        if (_keys[key].active == false)
        {
            _keys[key].active = true;
            _keys[key].action_started_ms = ms;
        }
        // Detect long-press when time runs out instead of waiting for release
        else
        {
            // Check time elapsed and confirm that we didn't set the start time IGNORE
            if (ms - _keys[key].action_started_ms > LONGPRESS_TIME && _keys[key].action_started_ms != IGNORE_KEY_EVENT)
            {
                result = eKeyStatus::LONG_PRESS;
                // Indicate we ignore further activity until the button is released
                _keys[key].action_started_ms = IGNORE_KEY_EVENT;
            }

        }
        
    }
    // Button is NOT pressed when DIGI_HIGH
    else
    {
        // If we'd previously marked the key as active
        if (_keys[key].active == true)
        {
            // Since the button has been released, mark it as inactive
            _keys[key].active = false;

            // If we're not supposed to ignore this, it must be a press-and-release event
            if(_keys[key].action_started_ms != IGNORE_KEY_EVENT)
            {
                // If the last SHORT_PRESS was within DOUBLETAP_DETECT_TIME, immediately return a DOUBLETAP event
                if(ms - _keys[key].last_tap_ms < DOUBLETAP_DETECT_TIME)
                {
                    _keys[key].last_tap_ms = 0; // Reset this so we don't keep counting it                    
                    result = eKeyStatus::DOUBLE_TAP;
                }
                // Otherwise just store when this event happened so we can check for it later
                else
                {
                    _keys[key].last_tap_ms = ms;
                }
            }
        }
        // If there's a last SHORT_PRESS time recorded, see if DOUBLETAP_DETECT_TIME has elapsed
        else
        {
            if(_keys[key].last_tap_ms != 0 && ms - _keys[key].last_tap_ms > DOUBLETAP_DETECT_TIME)
            {
                _keys[key].last_tap_ms = 0; // Reset this so we don't keep counting it
                result = eKeyStatus::SHORT_PRESS;
            }
        }
        
    }

    return result;
}

void KeyManager::_keystate_task(void *param)
{
    #define BLUETOOTH_LED eLed::LED_BT

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

                // Save Bluetooth status in fnConfig
                Config.store_bt_status(false); // Disabled
                Config.save();
            }
            else
            {
                // Stop WiFi
                fnWiFi.stop();

                fnLedManager.set(BLUETOOTH_LED, true); // BT LED ON
                fnBtManager.start();

                // Save Bluetooth status in fnConfig
                Config.store_bt_status(true); // Enabled
                Config.save();
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

        case eKeyStatus::DOUBLE_TAP:
            Debug_println("BUTTON_A: DOUBLE-TAP");
            break;

        default:
            break;
        } // BUTTON_A

        // Check on the status of the BUTTON_B and do something useful
        switch (pKM->getKeyStatus(eKey::BUTTON_B))
        {
        case eKeyStatus::LONG_PRESS:
            // Check if we're with a few seconds of booting and disable this button if so -
            // assume the button is stuck/disabled/non-existant
            if(fnSystem.millis() < 3000)
            {
                Debug_println("BUTTON_B: SEEMS STUCK - DISABLING");
                pKM->_keys[eKey::BUTTON_B].disabled = true;
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

        case eKeyStatus::DOUBLE_TAP:
            Debug_println("BUTTON_B: DOUBLE-TAP");
            fnSystem.debug_print_tasks();
            break;

        default:
            break;
        } // BUTTON_B

        if (pKM->has_button_c)
        {
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

            case eKeyStatus::DOUBLE_TAP:
                Debug_println("BUTTON_C: DOUBLE-TAP");
                break;

            default:
                break;
            } // BUTTON_C
        }
    }
}
