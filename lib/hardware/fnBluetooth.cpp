
#include "../../include/debug.h"
#include "../sio/sio.h"
#include "../sio/disk.h"
#include "fnBluetooth.h"
#include "fnBluetoothSPP.h"

#if defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED)

fnBluetoothSPP btSpp;

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
    btSpp.begin(BT_NAME);
    _mActive = true;
    SIO.setBaudrate(_mBTBaudrate);
}

void BluetoothManager::stop()
{
    Debug_println("STOP SIO2BT");
    _mActive = false;
    SIO.setBaudrate(eBTBaudrate::BT_STANDARD_BAUDRATE);
    btSpp.end();
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
        btSpp.write(fnUartSIO.read());
    }
    if (btSpp.available())
    {
        fnUartSIO.write(btSpp.read());
    }
}

#endif
