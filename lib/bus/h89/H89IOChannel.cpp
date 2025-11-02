#include "H89IOChannel.h"
#include "h89.h"

size_t H89IOChannel::dataOut(const void *buffer, size_t length)
{
    SYSTEM_BUS.port_putbuf(buffer,length);
}

void H89IOChannel::updateFIFO()
{
    while(SYSTEM_BUS.bus_available())
    {
        _fifo += SYSTEM_BUS.port_getc();
    }
}