#include "../../include/debug.h"
#include "bluetooth.h"
#include "../sio/sio.h"
#include "../sio/disk.h"
#include "BluetoothSerial.h"

BluetoothSerial SerialBT;

void BluetoothManager::start()
{
#ifdef DEBUG
    Debug_println("START SIO2BT");
#endif
    SerialBT.begin(BT_NAME);
    mActive = true;
    SIO.setBaudrate(mBTBaudrate);
}

void BluetoothManager::stop()
{
#ifdef DEBUG
    Debug_println("STOP SIO2BT");
#endif
    mActive = false;
    SIO.setBaudrate(eBTBaudrate::BT_STANDARD_BAUDRATE);
    SerialBT.end();
}

eBTBaudrate BluetoothManager::toggleBaudrate()
{
    if (mBTBaudrate == eBTBaudrate::BT_STANDARD_BAUDRATE)
    {
        mBTBaudrate = eBTBaudrate::BT_HISPEED_BAUDRATE;
    }
    else
    {
        mBTBaudrate = eBTBaudrate::BT_STANDARD_BAUDRATE;
    }
    SIO.setBaudrate(mBTBaudrate);
    return mBTBaudrate;
}

void BluetoothManager::service()
{
    /*
    if (SIO_UART.available()) {
      SerialBT.write(SIO_UART.read());
    }
    if (SerialBT.available()) {
      SIO_UART.write(SerialBT.read());
    }
    */
    if (fnUartSIO.available())
    {
        SerialBT.write(fnUartSIO.read());
    }
    if (SerialBT.available())
    {
        fnUartSIO.write(SerialBT.read());
    }
}
