#ifndef DEVICE_DB_H
#define DEVICE_DB_H

//#include "global_defines.h"
//#include <ArduinoJson.h>
#include <string>
#include "../FileSystem/fnFS.h"

#define RECORD_SIZE 256

class DeviceDB
{
public:
    DeviceDB(FileSystem *fs);
    ~DeviceDB();

    bool init(std::string database);
    bool check();
    bool save();

    std::string database;

    uint8_t device();
    void device(uint8_t device);
    uint8_t drive();
    void drive(uint8_t drive);
    uint8_t partition();
    void partition(uint8_t partition);
    std::string url();
    void url(std::string url);
    std::string path();
    void path(std::string path);
    std::string image();
    void image(std::string image);

    bool select(uint8_t device);

private:
    bool m_dirty;
    FileSystem *m_fileSystem;
    StaticJsonDocument<256> m_device;
};

#endif
