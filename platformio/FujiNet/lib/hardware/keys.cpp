#include "keys.h"

eKeyStatus KeyManager::getBootKeyStatus()
{
    eKeyStatus result = eKeyStatus::RELEASED;

    if (digitalRead(PIN_BOOT_BUTTON) == LOW)
    {
        if (mButtonActive == false)
        {
            mButtonActive = true;
            mButtonTimer = millis();
        }
        if ((millis() - mButtonTimer > LONGPRESS_TIME) && (mLongPressActive == false))
        {
            mLongPressActive = true;
            // long press detected
            result = eKeyStatus::LONG_PRESSED;
        }
    }
    else
    {
        if (mButtonActive == true)
        {
            if (mLongPressActive == true)
            {
                mLongPressActive = false;
                // long press released
            }
            else
            {
                // short press released
                result = eKeyStatus::SHORT_PRESSED;
            }
            mButtonActive = false;
        }
    }
    return result;
}