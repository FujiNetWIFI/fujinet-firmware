#ifndef CASSETTE_H
#define CASSETTE_H

#include "../../include/pinmap.h"

#include "bus.h"
#include "fnSystem.h"

class drivewireCassette : public virtualDevice
{
protected:

public:

TaskHandle_t playTask = NULL;

virtual void setup();
virtual void shutdown();
void play();

private:

};

#endif