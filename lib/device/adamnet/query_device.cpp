#ifdef BUILD_ADAM

#include "query_device.h"

// ctor
adamQueryDevice::adamQueryDevice()
{
}

// dtor
adamQueryDevice::~adamQueryDevice()
{
}

// virtual functions

void adamQueryDevice::adamnet_control_ready() { }
void adamQueryDevice::shutdown() { }
void adamQueryDevice::adamnet_process(uint8_t b) { }
void adamQueryDevice::adamnet_control_status() { }
void adamQueryDevice::adamnet_control_receive()  { }
void adamQueryDevice::adamnet_control_clr() { }

#endif /* BUILD_ADAM */
