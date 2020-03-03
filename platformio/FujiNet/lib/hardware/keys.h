#ifndef KEYS_H
#define KEYS_H

#include <Arduino.h>
#include "debug.h"

#define PIN_BOOT_BUTTON 0
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
private:
    long buttonTimer = 0;
    boolean buttonActive = false;
    boolean longPressActive = false;
};

#endif // guard