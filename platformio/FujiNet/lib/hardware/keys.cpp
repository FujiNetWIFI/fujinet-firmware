#include "keys.h"

eKeyStatus KeyManager::getBootKeyStatus()
{
    eKeyStatus result = eKeyStatus::RELEASED;

    if (digitalRead(PIN_BOOT_BUTTON) == LOW)
    {
        if (buttonActive == false)
        {
            buttonActive = true;
            buttonTimer = millis();
        }
        if ((millis() - buttonTimer > LONGPRESS_TIME) && (longPressActive == false))
        {
            longPressActive = true;
            // long press detected
            result = eKeyStatus::LONG_PRESSED;
        }
    }
    else
    {
        if (buttonActive == true)
        {
            if (longPressActive == true)
            {
                longPressActive = false;
                // long press released
            }
            else
            {
                // short press released
                result = eKeyStatus::SHORT_PRESSED;
            }
            buttonActive = false;
        }
    }
    return result;
}