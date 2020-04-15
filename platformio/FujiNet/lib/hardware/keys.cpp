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

eKeyStatus KeyManager::getOtherKeyStatus()
{
    eKeyStatus result = eKeyStatus::RELEASED;

    if (digitalRead(PIN_OTHER_BUTTON) == LOW)
    {
        if (oButtonActive == false)
        {
            oButtonActive = true;
            oButtonTimer = millis();
        }
        if ((millis() - oButtonTimer > LONGPRESS_TIME) && (oLongPressActive == false))
        {
            oLongPressActive = true;
            // long press detected
            result = eKeyStatus::LONG_PRESSED;
        }
    }
    else
    {
        if (oButtonActive == true)
        {
            if (oLongPressActive == true)
            {
                oLongPressActive = false;
                // long press released
            }
            else
            {
                // short press released
                result = eKeyStatus::SHORT_PRESSED;
            }
            oButtonActive = false;
        }
    }
    return result;
}
