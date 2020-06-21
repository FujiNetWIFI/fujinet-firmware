#include <cstring>

#include "fnSystem.h"
#include "led.h"
#include "keys.h"

KeyManager fnKeyManager;

void KeyManager::setup()
{
    fnSystem.set_pin_mode(PIN_BOOT_KEY, PINMODE_INPUT);
    fnSystem.set_pin_mode(PIN_OTHER_KEY, PINMODE_INPUT);

    // Start a new task to check the status of the buttons
    xTaskCreate(_keystate_task, "fnKeys", 4096, this, 1, nullptr);
}

bool KeyManager::keyCurrentlyPressed(eKey key)
{
    return (fnSystem.digital_read(mButtonPin[key]) == DIGI_LOW);
}

eKeyStatus KeyManager::getKeyStatus(eKey key)
{
    eKeyStatus result = eKeyStatus::RELEASED;

    if (fnSystem.digital_read(mButtonPin[key]) == DIGI_LOW)
    {
        if (mButtonActive[key] == false)
        {
            mButtonActive[key] = true;
            mButtonTimer[key] = fnSystem.millis();
        }
        if ((fnSystem.millis() - mButtonTimer[key] > LONGPRESS_TIME) && (mLongPressActive[key] == false))
        {
            mLongPressActive[key] = true;
            // long press detected
            result = eKeyStatus::LONG_PRESSED;
        }
    }
    else
    {
        if (mButtonActive[key] == true)
        {
            if (mLongPressActive[key] == true)
            {
                mLongPressActive[key] = false;
                // long press released
            }
            else
            {
                // short press released
                result = eKeyStatus::SHORT_PRESSED;
            }
            mButtonActive[key] = false;
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
        vTaskDelay(200 / portTICK_PERIOD_MS);

        // Check on the status of the OTHER_KEY and do something useful
        switch (pKM->getKeyStatus(eKey::OTHER_KEY))
        {
        case eKeyStatus::LONG_PRESSED:
            Debug_println("O_KEY: LONG PRESS");
            break;
        case eKeyStatus::SHORT_PRESSED:
            Debug_println("O_KEY: SHORT PRESS");
            break;
        default:
            break;
        }

        // Check on the status of the BOOT_KEY and do something useful
        switch (pKM->getKeyStatus(eKey::BOOT_KEY))
        {
        case eKeyStatus::LONG_PRESSED:
            Debug_println("B_KEY: LONG PRESS");

#ifdef BLUETOOTH_SUPPORT
            if (btMgr.isActive())
            {
                btMgr.stop();
                ledMgr.set(BLUETOOTH_LED, false);
            }
            else
            {
                ledMgr.set(BLUETOOTH_LED, true); // SIO LED always ON in Bluetooth mode
                btMgr.start();
            }
#endif //BLUETOOTH_SUPPORT
            break;
        case eKeyStatus::SHORT_PRESSED:
            Debug_println("B_KEY: SHORT PRESS");
            fnLedManager.blink(BLUETOOTH_LED); // blink to confirm a button press

// Either toggle BT baud rate or do a disk image rotation on B_KEY SHORT PRESS
#ifdef BLUETOOTH_SUPPORT
            if (btMgr.isActive())
                btMgr.toggleBaudrate();
            else
#endif
                Debug_println("TODO: Re-connect theFuji.image_rotate()!!");
            //theFuji.image_rotate();
            break;
        default:
            break;
        }
    }
}
