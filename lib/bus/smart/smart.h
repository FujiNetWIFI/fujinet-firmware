#ifndef SMART_H
#define SMART_H

#include "bus.h"

class smartDevice
{

public:
bool device_active;
virtual void shutdown() = 0;

};

class smartBus
{

public:
void shutdown() {};

};

extern smartBus SmartPort;

#endif // guard

