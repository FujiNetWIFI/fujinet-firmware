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
virtual void drivewire_process(uint32_t commanddata, uint8_t checksum);
virtual void shutdown();
void play();
void stop();

private:

};

#endif