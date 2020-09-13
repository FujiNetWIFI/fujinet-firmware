#ifndef KEYS_H
#define KEYS_H

enum eKey
{
    BUTTON_A = 0,
    BUTTON_B,
    BUTTON_C,
    KEY_COUNT
};

enum eKeyStatus
{
    DISABLED,
    INACTIVE,
    SINGLE_TAP,
    DOUBLE_TAP,
    SHORT_PRESS,
    LONG_PRESS
};

class KeyManager
{
public:
    void setup();
    eKeyStatus getKeyStatus(eKey key);
    bool keyCurrentlyPressed(eKey key);
    void ignoreKeyPress(eKey key);
    bool buttonCavail = false;
    bool getCAvail();

private:
    long _buttonLastTap[eKey::KEY_COUNT] = {0};
    long _buttonActionStarted[eKey::KEY_COUNT] = {0};
    bool _buttonActive[eKey::KEY_COUNT] = { false };
    bool _buttonDisabled[eKey::KEY_COUNT] = { false };


    static void _keystate_task(void *param);
};

// Global KeyManager object
extern KeyManager fnKeyManager;

#endif
