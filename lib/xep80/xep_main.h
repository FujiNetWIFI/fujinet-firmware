#ifndef XEP_MAIN_H
#define XEP_MAIN_H

#include "fnSystem.h"

//#include <driver/ledc.h>
//#include "sio.h"
//#include "../tcpip/fnUDP.h"

class xep_main
{
protected:
    uint8_t denoise_counter = 0;
    const uint16_t period_space = 1000000 / 3995;
    const uint16_t period_mark = 1000000 / 5327;

private:
    unsigned short block;
    unsigned short baud;

public:
    bool service();
    void receive_word();
    void process_word(uint16_t W);
} ;

#endif