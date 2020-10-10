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
    DOUBLE_TAP,
    SHORT_PRESS,
    LONG_PRESS
};

struct _key_t
{
    long last_tap_ms = 0;
    long previous_tap_ms = 0;
    long action_started_ms = 0;

    bool active = false;
    bool disabled = false;
};

class KeyManager
{
public:
    void setup();
    eKeyStatus getKeyStatus(eKey key);
    bool keyCurrentlyPressed(eKey key);
    void ignoreKeyPress(eKey key);

    bool has_button_c = false;

private:
    _key_t _keys[eKey::KEY_COUNT];

    static void _keystate_task(void *param);
};

// Global KeyManager object
extern KeyManager fnKeyManager;

#endif
