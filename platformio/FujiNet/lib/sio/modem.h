#ifndef MODEM_H
#define MODEM_H
#include <Arduino.h>
#include "sio.h"

class sioModem : public sioDevice
{
  private:
    bool modemActive = false;
    long modemBaud = 2400;          // Default modem baud rate
    bool DTR = false;
    bool RTS = false;
    bool XMT = false;

    void sio_relocator();           // $21, '!', Booter/Relocator download
    void sio_handler();             // $26, '&', Handler download
    void sio_poll_1();              // $3F, '?', Type 1 Poll
    void sio_control();             // $41, 'A', Control
    void sio_config();              // $42, 'B', Configure
    void sio_status() override;     // $53, 'S', Status
    void sio_write();               // $57, 'W', Write
    void sio_stream();              // $58, 'X', Concurrent/Stream
    void sio_process() override;    // Process the command
  public:

};

#endif
