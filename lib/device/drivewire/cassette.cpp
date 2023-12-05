#ifdef BUILD_COCO

#include "cassette.h"

#include <cstring>

#include "../../include/debug.h"

#include "fnSystem.h"
#include "fnUART.h"
#include "fnFsSD.h"

#include "led.h"

void drivewireCassette::drivewire_process(uint32_t commanddata, uint8_t checksum)
{
    // Not really used at the moment.
}

#endif /* BUILD_COCO */