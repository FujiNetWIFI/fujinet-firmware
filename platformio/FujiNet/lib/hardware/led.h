#ifndef LED_H
#define LED_H

#define BLINKING_TIME 200 // 200 ms

enum eLed
{
    LED_WIFI = 0,
    LED_SIO,
    LED_BT,
    LED_COUNT
};

class LedManager
{
public:
    LedManager();
    void setup();
    void set(eLed led, bool on=true);
    void toggle(eLed led);
    void blink(eLed led);

private:
    bool mLedState[eLed::LED_COUNT];
    int mLedPin[eLed::LED_COUNT];
};

extern LedManager ledMgr;

#endif // guard
