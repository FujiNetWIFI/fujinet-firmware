#ifndef KEYS_H
#define KEYS_H

enum eKey
{
    BUTTON_A = 0,
    BUTTON_B,
    KEY_COUNT
};

enum eKeyStatus
{
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

private:
    long _buttonLastTap[eKey::KEY_COUNT] = {0};
    long _buttonActionStarted[eKey::KEY_COUNT] = {0};
    bool _buttonActive[eKey::KEY_COUNT] = {0};

    static void _keystate_task(void *param);
};

// Global KeyManager object
extern KeyManager fnKeyManager;

#endif
