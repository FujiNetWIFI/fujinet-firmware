// Meatloaf - A Commodore 64/128 multi-device emulator
// https://github.com/idolpx/meatloaf
// Copyright(C) 2020 James Johnston
//
// This file is part of Meatloaf but adapted for use in the FujiNet project
// https://github.com/FujiNetWIFI/fujinet-platformio
// 
// Meatloaf is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// Meatloaf is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with Meatloaf. If not, see <http://www.gnu.org/licenses/>.

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
    DeviceDB();
    ~DeviceDB();

    bool init(std::string database, FileSystem *fs);
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
    // StaticJsonDocument<256> m_device;
};

#endif
