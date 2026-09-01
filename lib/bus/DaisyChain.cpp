#include "DaisyChain.h"
#include <algorithm>

DaisyChain::Entry *DaisyChain::entryForDevice(virtualDevice *device)
{
    auto it = std::find_if(_daisyChain.begin(), _daisyChain.end(),
                           [device](const DaisyChain::Entry &entry) {
                               return entry.device == device;
                           });

    return it != _daisyChain.end() ? &*it : nullptr;
}

DaisyChain::Entry *DaisyChain::entryForFujiID(fujiDeviceID_t fujiID)
{
    auto it = std::find_if(_daisyChain.begin(), _daisyChain.end(),
                           [fujiID](const DaisyChain::Entry &entry) {
                               return entry.fujiID == fujiID;
                           });

    return it != _daisyChain.end() ? &*it : nullptr;
}

virtualDevice *DaisyChain::deviceWithFujiID(fujiDeviceID_t fujiID)
{
    auto entry = entryForFujiID(fujiID);
    return entry ? entry->device : nullptr;
}

std::optional<fujiDeviceID_t> DaisyChain::fujiIDForDevice(virtualDevice *device)
{
    auto entry = entryForDevice(device);
    return entry ? std::optional<fujiDeviceID_t>(entry->fujiID) : std::nullopt;
}

void DaisyChain::addDevice(virtualDevice *newDev, fujiDeviceID_t devID)
{
    _daisyChain.push_front({newDev, devID});
    return;
}

void DaisyChain::assignFujiIDToDevice(virtualDevice *device, fujiDeviceID_t fujiID)
{
    auto entry = entryForDevice(device);

    if (entry)
        entry->fujiID = fujiID;
    return;
}
