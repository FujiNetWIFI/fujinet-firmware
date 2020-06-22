#ifndef KEYS_H
#define KEYS_H

#define LONGPRESS_TIME 1000 // 1 second
#define PIN_BOOT_KEY 0
#define PIN_OTHER_KEY 34
enum eKey
{
    BOOT_KEY = 0,
    OTHER_KEY,
    KEY_COUNT
};

enum eKeyStatus
{
    RELEASED,
    SHORT_PRESSED,
    LONG_PRESSED
};

static const int mButtonPin[eKey::KEY_COUNT] = {PIN_BOOT_KEY, PIN_OTHER_KEY};

extern TaskHandle_t handle_WiFi;

class KeyManager
{
public:
    void setup();
    eKeyStatus getKeyStatus(eKey key);
    static bool keyCurrentlyPressed(eKey key);


private:
    long mButtonTimer[eKey::KEY_COUNT] = {0};
    bool mButtonActive[eKey::KEY_COUNT] = {0};
    bool mLongPressActive[eKey::KEY_COUNT] = {0};

    static void _keystate_task(void *param);
};

extern KeyManager fnKeyManager;

#endif
