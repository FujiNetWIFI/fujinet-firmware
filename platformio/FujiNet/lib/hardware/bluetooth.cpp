#include "bluetooth.h"
#include "sio.h"
#include "disk.h"
#include "BluetoothSerial.h"

BluetoothSerial SerialBT;

void BluetoothManager::start()
{
  SerialBT.begin(BT_NAME);
  SIO.setBaudrate(STANDARD_BAUDRATE);
  SIO.sio_led(true);
}

void BluetoothManager::stop()
{
  SerialBT.end();
  SIO.sio_led(false);
}

void BluetoothManager::service()
{
    if (SIO_UART.available()) {
      SerialBT.write(SIO_UART.read());
    }
    if (SerialBT.available()) {
      SIO_UART.write(SerialBT.read());
    }
}