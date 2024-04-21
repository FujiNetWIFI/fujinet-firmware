#ifndef ESP_PLATFORM

#if defined(BUILD_ATARI) || defined(BUILD_COCO)

#include "serialsio.h"
#include "fnSystem.h"

void SerialSioPort::bus_idle(uint16_t ms)
{
    fnSystem.delay(ms);
}

#endif // BUILD_ATARI

#endif // !ESP_PLATFORM