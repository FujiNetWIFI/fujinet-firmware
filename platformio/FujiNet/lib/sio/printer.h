#ifndef PRINTER_H
#define PRINTER_H
#include <Arduino.h>
#include <string.h>
#include <FS.h>
#include <SPIFFS.h>

#include "sio.h"
#include "printer_emulator.h"

class sioPrinter : public sioDevice
{
protected:
    // does not work: friend void atari820::pdf_handle_char(byte c);
    // SIO THINGS
    byte buffer[40];
    void sio_write();
    void sio_status();
    void sio_process();
    byte lastAux1 = 0;

    /**
     * new design idea:
     * remove pure virtual functions
     * replace with pointer to printer emulator objects
     * so printer emaulator code can be reused by non-SIO
     * applications
     * 
     * */
    printer_emu *_pptr;

public:
    void connect_printer(printer_emu *P) { _pptr = P; };
};


#endif // guard