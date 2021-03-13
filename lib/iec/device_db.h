#ifndef DEVICE_DB_H
#define DEVICE_DB_H

#include "global_defines.h"
#include <ArduinoJson.h>

#if defined(USE_SPIFFS)
#include <SPIFFS.h>
#elif defined(USE_LITTLEFS)
#if defined(ESP8266)
#include <LittleFS.h>
#elif defined(ESP32)
#include <LITTLEFS.h>
#endif
#endif

#define RECORD_SIZE 256

class DeviceDB
{
public:
    DeviceDB(FS *fileSystem);
    ~DeviceDB();

    bool init(String database);
    bool check();
    bool save();

    String database;

    byte device();
    void device(byte device);
    byte drive();
    void drive(byte drive);
    byte partition();
    void partition(byte partition);
    String url();
    void url(String url);
    String path();
    void path(String path);
    String image();
    void image(String image);

    bool select(byte device);

private:
    bool m_dirty;
    FS *m_fileSystem;
    StaticJsonDocument<256> m_device;
};

#endif
