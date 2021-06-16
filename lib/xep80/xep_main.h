#ifndef XEP_MAIN_H
#define XEP_MAIN_H

#include "fnSystem.h"

//#include <driver/ledc.h>
//#include "sio.h"
//#include "../tcpip/fnUDP.h"

class xep_main
{
protected:
    uint16_t out[10];

public:
    bool service();
    void receive_word();
    void process_word(uint16_t W);
} ;

#endif