#include "bus.h"

void SystemBusBase::setDeviceEnabled(fujiDeviceID_t device_id, bool enabled)
{
    virtualDevice *device = _daisyChain.deviceWithFujiID(device_id);
    if (device)
        device->device_active = enabled;
}

