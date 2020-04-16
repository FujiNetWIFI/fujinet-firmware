#ifndef KEYS_H
#define KEYS_H

#define LONGPRESS_TIME 1000 // 1 second

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

class KeyManager
{
public:
    KeyManager();
    void setup();
    eKeyStatus getKeyStatus(eKey key);

private:
    int mButtonPin[eKey::KEY_COUNT];
    long mButtonTimer[eKey::KEY_COUNT];
    bool mButtonActive[eKey::KEY_COUNT];
    bool mLongPressActive[eKey::KEY_COUNT];
};

extern KeyManager keyMgr;

#endif // guard
