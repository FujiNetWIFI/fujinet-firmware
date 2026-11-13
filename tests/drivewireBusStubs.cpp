// The drivewire systemBus overrides setFuji() in drivewire.cpp, which the COCO
// tests don't link.

#include "bus.h"

void systemBus::setFuji(fujiDevice *) {}
