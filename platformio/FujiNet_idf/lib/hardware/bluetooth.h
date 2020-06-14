#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#define BT_NAME "SIO2BT FUJINET"

enum eBTBaudrate
{
    BT_STANDARD_BAUDRATE = 19200,
    BT_HISPEED_BAUDRATE = 57600
};

class BluetoothManager
{
public:
    inline bool isActive() { return _mActive; };
    void start();
    void stop();
    eBTBaudrate toggleBaudrate();
    void service();
private:
    eBTBaudrate _mBTBaudrate = eBTBaudrate::BT_STANDARD_BAUDRATE;
    bool _mActive = false;
};

#endif // guard
