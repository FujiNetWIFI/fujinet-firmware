#ifndef KEYS_H
#define KEYS_H

#include <Arduino.h>
#include "debug.h"

#define PIN_BOOT_BUTTON 0
#define PIN_OTHER_BUTTON 34
#define LONGPRESS_TIME 1000 // 1 second

enum eKeyStatus
{
    RELEASED,
    SHORT_PRESSED,
    LONG_PRESSED
};

class KeyManager
{
public:
    eKeyStatus getBootKeyStatus();
    eKeyStatus getOtherKeyStatus();
private:
    long mButtonTimer = 0;
    boolean mButtonActive = false;
    boolean mLongPressActive = false;
    long oButtonTimer = 0;
    boolean oButtonActive = false;
    boolean oLongPressActive = false;
};

#endif // guard
