#ifndef DISK_H
#define DISK_H
#include <Arduino.h>
#include "sio.h"
//#include "tnfs.h"

#ifdef ESP_8266
#include <FS.h>
#define INPUT_PULLDOWN INPUT_PULLDOWN_16 // for motor pin
#elif defined(ESP_32)
#include <SPIFFS.h>
#endif

// #define DELAY_T5 1500
// #define READ_CMD_TIMEOUT 12
// #define CMD_TIMEOUT 50
// #define STATUS_SKIP 8

class sioDisk : public sioDevice
{
private:
    File *_file;
    int _devnum = 0x31;

    byte sector[128];

    //    byte sio_checksum(byte *chunk, int length);
    //    void sio_get_id();
    //    void sio_get_command();
    //    void sio_get_aux1();
    //    void sio_get_aux2();
    void sio_read();
    void sio_write();
    void sio_format();
    void sio_status() override;
    void sio_process() override;
    //    void sio_ack();
    //    void sio_nak();
    //    void sio_get_checksum();
    //    void sio_incoming();

public:
    sioDisk(){};
    sioDisk(int devnum) : _devnum(devnum){};
    void mount(File *f);
    // void handle();
};

#endif // guard