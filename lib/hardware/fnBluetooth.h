#ifndef BLUETOOTH_H
#define BLUETOOTH_H

enum eBTBaudrate
{
    BT_STANDARD_BAUDRATE = 19200,
    BT_HISPEED_BAUDRATE =  57600
};

class BluetoothManager
{
private:
    eBTBaudrate _mBTBaudrate = BT_STANDARD_BAUDRATE;
    bool _mActive = false;

public:
    inline bool isActive() { return _mActive; };
    void start();
    void stop();
    eBTBaudrate toggleBaudrate();
    void service();
};

extern BluetoothManager fnBtManager;

#endif

