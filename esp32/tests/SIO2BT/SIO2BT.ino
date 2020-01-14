//This code is based on the example code from Evandro Copercini - 2018

#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

#define RXD2 16
#define TXD2 17

BluetoothSerial SerialBT;

void setup() {
  Serial2.begin(57600, SERIAL_8N1, RXD2, TXD2);
  SerialBT.begin("ATARI FUJINET");
}

void loop() {
  if (Serial2.available()) {
    SerialBT.write(Serial2.read());
  }
  if (SerialBT.available()) {
    Serial2.write(SerialBT.read());
  }
}
