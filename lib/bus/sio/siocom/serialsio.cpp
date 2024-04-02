#ifndef ESP_PLATFORM

#ifdef BUILD_ATARI

#include "serialsio.h"
#include "fnSystem.h"

void SerialSioPort::bus_idle(uint16_t ms)
{
    fnSystem.delay(ms);
}

#endif // BUILD_ATARI

#endif // !ESP_PLATFORM