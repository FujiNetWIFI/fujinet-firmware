#ifdef BUILD_CX16

#include "cx16_i2c.h"
#include "../../include/debug.h"

uint8_t cx16_checksum(uint8_t *buf, unsigned short len)
{
    unsigned int chk = 0;

    for (int i = 0; i < len; i++)
        chk = ((chk + buf[i]) >> 8) + ((chk + buf[i]) & 0xff);

    return chk;
}

void virtualDevice::bus_to_computer(uint8_t *buf, uint16_t len, bool err)
{
    // TODO IMPLEMENT
}

uint8_t virtualDevice::bus_to_peripheral(uint8_t *buf, unsigned short len)
{
    uint8_t ck = 0;

    // TODO IMPLEMENT 

    return ck;
}

void virtualDevice::cx16_nak()
{
    // TODO IMPLEMENT
}

void virtualDevice::cx16_ack()
{
    // TODO IMPLEMENT
}

void virtualDevice::cx16_complete()
{
    // TODO IMPLEMENT
}

void virtualDevice::cx16_error()
{
    // TODO IMPLEMENT
}

systemBus virtualDevice::get_bus()
{
    return CX16;
}


void systemBus::process_cmd()
{
    // TODO IMPLEMENT
}

void systemBus::process_queue()
{
    // TODO IMPLEMENT
}

void systemBus::service()
{
    // TODO IMPLEMENT
}

void systemBus::setup()
{
    // TODO IMPLEMENT
}

void systemBus::addDevice(virtualDevice *pDevice, int device_id)
{
    if (!pDevice)
    {
        Debug_printf("systemBus::addDevice() pDevice == nullptr! returning.\n");
        return;
    }

    // TODO, add device shortcut pointer logic like others

    pDevice->_devnum = device_id;
    _daisyChain.push_front(pDevice);    
}

void systemBus::remDevice(virtualDevice *pDevice)
{
    if (!pDevice)
    {
        Debug_printf("system Bus::remDevice() pDevice == nullptr! returning\n");
        return;
    }

    _daisyChain.remove(pDevice);
}

void systemBus::changeDeviceId(virtualDevice *pDevice, int device_id)
{
    if (!pDevice)
    {
        Debug_printf("systemBus::changeDeviceId() pDevice == nullptr! returning.\n");
        return;
    }

    for (auto devicep : _daisyChain)
    {
        if (devicep == pDevice)
            devicep->_devnum = device_id;
    }
}

virtualDevice *systemBus::deviceById(int device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep->_devnum == device_id)
            return devicep;
    }
    return nullptr;
}

void systemBus::shutdown()
{
    shuttingDown = true;

    for (auto devicep : _daisyChain)
    {
        Debug_printf("Shutting down device %02x\n",devicep->id());
        devicep->shutdown();
    }
    Debug_printf("All devices shut down.\n");
}

systemBus CX16;

#endif /* BUILD_CX16 */