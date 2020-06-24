/* Largely based on BluetoothSerial.cpp/h from Arduino-ESP
	2018 Evandro Luis Copercini
*/

#ifndef _FN_BLUETOOTH_SERIAL_H_
#define _FN_BLUETOOTH_SERIAL_H_

#include "sdkconfig.h"

#if defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED)

#include <string>
#include <functional>
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_spp_api.h"

using namespace std;

typedef function<void(const uint8_t *buffer, size_t size)> fnBluetoothDataCb;

class fnBluetooth
{
public:
    fnBluetooth();
    ~fnBluetooth();

    bool begin(string localName = string(), bool isMaster = false);
    int available(void);
    int peek(void);
    bool hasClient(void);
    int read(void);
    size_t write(uint8_t c);
    size_t write(const uint8_t *buffer, size_t size);
    void flush();
    void end(void);
    void onData(fnBluetoothDataCb cb);
    esp_err_t register_callback(esp_spp_cb_t *callback);

    void enableSSP();
    bool setPin(const char *pin);
    bool connect(std::string remoteName);
    bool connect(uint8_t remoteAddress[]);
    bool connect();
    bool connected(int timeout = 0);
    bool isReady(bool checkMaster = false, int timeout = 0);
    bool disconnect();
    bool unpairDevice(uint8_t remoteAddress[]);

private:
    string local_name;
};

#endif // defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED)

#endif // _FN_BLUETOOTH_SERIAL_H_
