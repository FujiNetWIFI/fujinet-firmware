
#include "../../include/debug.h"
#include "../sio/sio.h"
#include "../sio/disk.h"
#include "fnBluetooth.h"
#include "fnBluetoothSPP.h"

#ifdef BLUETOOTH_SUPPORT

fnBluetoothSPP btSpp;

BluetoothManager fnBtManager;

#define BT_NAME "SIO2BT #FujiNet"

void BluetoothManager::start()
{
    Debug_println("Starting SIO2BT");
    btSpp.begin(BT_NAME);
    _mActive = true;
    SIO.setBaudrate(_mBTBaudrate);
}

void BluetoothManager::stop()
{
    Debug_println("Stopping SIO2BT");
    _mActive = false;
    SIO.setBaudrate(BT_STANDARD_BAUDRATE);
    btSpp.end();
}

eBTBaudrate BluetoothManager::toggleBaudrate()
{
    _mBTBaudrate =
        _mBTBaudrate == BT_STANDARD_BAUDRATE ? BT_HISPEED_BAUDRATE : BT_STANDARD_BAUDRATE;

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
