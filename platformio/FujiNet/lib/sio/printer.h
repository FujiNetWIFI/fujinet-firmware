#ifndef PRINTER_H
#define PRINTER_H
#include <Arduino.h>

#include "sio.h"

class sioPrinter : public sioDevice
{
private:

    byte buffer[40];

    //    byte sio_checksum(byte *chunk, int length);
    //    void sio_get_id();
    //    void sio_get_command();
    //    void sio_get_aux1();
    //    void sio_get_aux2();
    // void sio_read();
    void sio_write();
    // void sio_format();
    void sio_status() override;
    void sio_process() override;
    //    void sio_ack();
    //    void sio_nak();
    //    void sio_get_checksum();
    //    void sio_incoming();

public:
    //sioDisk(){};
    //sioDisk(int devnum=0x31) : _devnum(devnum){};
    // void mount(File *f);
    // void handle();
};

#endif // guard