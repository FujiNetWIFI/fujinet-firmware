#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <Arduino.h>
#include "debug.h"

#define BT_NAME "ATARI FUJINET"

class BluetoothManager
{
public:
    void start();
    void stop();
    void service();
};

#endif // guard