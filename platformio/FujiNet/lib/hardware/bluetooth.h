#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <Arduino.h>
#include "debug.h"

#define BT_NAME "ATARI FUJINET"

enum eBTBaudrate
{
    BT_STANDARD_BAUDRATE = 19200,
    BT_HISPEED_BAUDRATE = 57600
};

class BluetoothManager
{
public:
    void setup();
    bool isActive();
    void start();
    void stop();
    eBTBaudrate toggleBaudrate();
    void service();
private:
    eBTBaudrate mBTBaudrate = eBTBaudrate::BT_STANDARD_BAUDRATE;
    int mPrevBaudrate = eBTBaudrate::BT_STANDARD_BAUDRATE;
    bool mActive = false;
};

inline bool BluetoothManager::isActive()
{
    return mActive;
}

#endif // guard