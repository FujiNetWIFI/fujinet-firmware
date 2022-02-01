#ifndef KEYS_H
#define KEYS_H

// #include <cstring>

// #include "fnSystem.h"
// #include "fnBluetooth.h"
// #include "fnWiFi.h"
// #include "led.h"

// #include "bus.h"
// #include "fnConfig.h"

// #include "../../include/pinmap.h"

#define LONGPRESS_TIME 1500 // 1.5 seconds to detect long press
#define DOUBLETAP_DETECT_TIME 400 // ms to wait to see if it's a single/double tap


#define IGNORE_KEY_EVENT -1

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
