
#include "../../include/debug.h"
#include "../sio/sio.h"
#include "../sio/disk.h"
#include "bluetooth.h"
#include "fnBluetooth.h"

#if defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED)

fnBluetooth SerialBT;

BluetoothManager fnBtManager;

/*
.begin
.end
.read
.write
*/

void BluetoothManager::start()
{
    Debug_println("START SIO2BT");
    SerialBT.begin(BT_NAME);
    _mActive = true;
    SIO.setBaudrate(_mBTBaudrate);
}

void BluetoothManager::stop()
{
    Debug_println("STOP SIO2BT");
    _mActive = false;
    SIO.setBaudrate(eBTBaudrate::BT_STANDARD_BAUDRATE);
    SerialBT.end();
}

eBTBaudrate BluetoothManager::toggleBaudrate()
{
    if (_mBTBaudrate == eBTBaudrate::BT_STANDARD_BAUDRATE)
    {
        _mBTBaudrate = eBTBaudrate::BT_HISPEED_BAUDRATE;
    }
    else
    {
        _mBTBaudrate = eBTBaudrate::BT_STANDARD_BAUDRATE;
    }
    SIO.setBaudrate(_mBTBaudrate);
    return _mBTBaudrate;
}

void BluetoothManager::service()
{
    if (fnUartSIO.available())
    {
        SerialBT.write(fnUartSIO.read());
    }
    if (SerialBT.available())
    {
        fnUartSIO.write(SerialBT.read());
    }
}

#endif
