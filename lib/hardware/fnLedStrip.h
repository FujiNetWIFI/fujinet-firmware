#ifndef LEDSTRIP_H
#define LEDSTRIP_H


class LedStripManager
{
public:
    enum eLedID
    {
        LED_STRIP_BUS,
        LED_STRIP_BT,
        LED_STRIP_WIFI
    };

    void setup();
    void set(eLedID id, bool on);
    void toggle(eLedID id);
    void blink(eLedID led, int count);
    bool present();

private:
// simple implementation - red - Bluetooth, Blue - WiFi, Green - Bus
    unsigned char r;
    unsigned char g;
    unsigned char b;

    static const unsigned char brightness = 64;
};

// Global LedStripManager object
extern LedStripManager fnLedStrip;


#endif