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

class KeyManager
{
public:
    KeyManager();
    void setup();
    eKeyStatus getKeyStatus(eKey key);
    static bool keyCurrentlyPressed(eKey key);

  //  static constexpr const int mButtonPin[eKey::KEY_COUNT] = {PIN_BOOT_KEY, PIN_OTHER_KEY};

private:

    long mButtonTimer[eKey::KEY_COUNT];
    bool mButtonActive[eKey::KEY_COUNT];
    bool mLongPressActive[eKey::KEY_COUNT];
};

extern KeyManager keyMgr;

#endif // guard
