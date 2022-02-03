#ifdef BLUETOOTH_SUPPORT

#include "fnBluetooth.h"

#include "../../include/debug.h"

#include "bus.h"
#include "fnConfig.h"
#include "fnBluetoothSPP.h"


fnBluetoothSPP btSpp;

BluetoothManager fnBtManager;

void BluetoothManager::start()
{
    int savedBaud = Config.get_bt_baud();
    int currBaud = _mBTBaudrate;

    Debug_println("Starting SIO2BT");
    if (savedBaud != currBaud)
    {
        switch (savedBaud)
        {
        case 57600:
            _mBTBaudrate = BT_HISPEED_BAUDRATE;
            break;
        case 19200:
            _mBTBaudrate = BT_STANDARD_BAUDRATE;
            break;
        default:
            break;
        }
    }
    btSpp.begin(Config.get_bt_devname());
    _mActive = true;
    systemBus.setBaudrate(_mBTBaudrate);
}

void BluetoothManager::stop()
{
    Debug_println("Stopping SIO2BT");
    _mActive = false;
    systemBus.setBaudrate(BT_STANDARD_BAUDRATE);
    btSpp.end();
}

eBTBaudrate BluetoothManager::toggleBaudrate()
{
    _mBTBaudrate =
        _mBTBaudrate == BT_STANDARD_BAUDRATE ? BT_HISPEED_BAUDRATE : BT_STANDARD_BAUDRATE;

    Config.store_bt_baud(_mBTBaudrate);
    Config.save();
    systemBus.setBaudrate(_mBTBaudrate);
    return _mBTBaudrate;
}

void BluetoothManager::service()
{
    if (fnUartSIO.available() > 0)
    {
        btSpp.write(fnUartSIO.read());
    }
    if (btSpp.available())
    {
        fnUartSIO.write(btSpp.read());
    }
}

#endif
