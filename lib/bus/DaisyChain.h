#ifndef DAISYCHAIN_H
#define DAISYCHAIN_H

#include "fujiDeviceID.h"

#include <optional>
#include <forward_list>
#include <vector>

class virtualDevice;

class DaisyChain
{
protected:
    struct Entry {
        virtualDevice *device;
        fujiDeviceID_t fujiID;
    };

    using Container = std::forward_list<DaisyChain::Entry>;
    Container _daisyChain;

    DaisyChain::Entry *entryForDevice(virtualDevice *device);
    DaisyChain::Entry *entryForFujiID(fujiDeviceID_t fujiID);

public:
    virtualDevice *deviceWithFujiID(fujiDeviceID_t fujiID);
    std::optional<fujiDeviceID_t> fujiIDForDevice(virtualDevice *device);

    void addDevice(virtualDevice *newDev, fujiDeviceID_t fujiID);
    void assignFujiIDToDevice(virtualDevice *device, fujiDeviceID_t fujiID);

    class iterator {
        friend class DaisyChain;

        using InternalIterator = Container::iterator;

        InternalIterator _it;
        iterator(InternalIterator it) : _it(it) {}

    public:
        virtualDevice *operator*() const {
            return _it->device;
        }

        iterator &operator++() {
            ++_it;
            return *this;
        }

        bool operator!=(const iterator &other) const {
            return _it != other._it;
        }
    };

    iterator begin() {
        return iterator(_daisyChain.begin());
    }

    iterator end() {
        return iterator(_daisyChain.end());
    }

    // Rotate the specified devices by the given index offset.
    // Positive values increase each device's index; negative values decrease it.
    // Indices wrap around within the supplied device sequence.
    template <typename T>
    requires std::derived_from<T, virtualDevice>
    void rotateDevices(const std::vector<T *> &devices, int amount) {
        size_t idx, len;

        if (devices.size() < 2 || amount == 0)
            return;

        std::vector<DaisyChain::Entry *> entries;

        for (auto *device : devices)
        {
            if (auto *entry = entryForDevice(device))
                entries.push_back(entry);
        }

        len = entries.size();
        if (len != devices.size())
            return;

        amount %= len;

        if (amount < 0)
            amount += len;

        std::vector<virtualDevice *> rotated(len);
        for (idx = 0; idx < len; idx++)
            rotated[(idx + amount) % len] = entries[idx]->device;

        for (idx = 0; idx < len; idx++)
            entries[idx]->device = rotated[idx];

        return;
    }
};

#endif /* DAISYCHAIN_H */
