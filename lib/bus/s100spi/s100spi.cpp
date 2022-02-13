#ifdef BUILD_S100

/**
 * s100spi Functions
 */
#include "s100spi.h"

#include "../../include/debug.h"

#include "fnSystem.h"
#include "led.h"

uint8_t s100spi_checksum(uint8_t *buf, unsigned short len)
{
    uint8_t checksum = 0x00;

    for (unsigned short i = 0; i < len; i++)
        checksum ^= buf[i];

    return checksum;
}

void virtualDevice::s100spi_send(uint8_t b)
{
}

void virtualDevice::s100spi_send_buffer(uint8_t *buf, unsigned short len)
{
}

uint8_t virtualDevice::s100spi_recv()
{
    return 0;
}

bool virtualDevice::s100spi_recv_timeout(uint8_t *b, uint64_t dur)
{
    return false;
}

uint16_t virtualDevice::s100spi_recv_length()
{
    unsigned short s = 0;
    s = s100spi_recv() << 8;
    s |= s100spi_recv();

    return s;
}

void virtualDevice::s100spi_send_length(uint16_t l)
{
    s100spi_send(l >> 8);
    s100spi_send(l & 0xFF);
}

unsigned short virtualDevice::s100spi_recv_buffer(uint8_t *buf, unsigned short len)
{
    return 0;
}

uint32_t virtualDevice::s100spi_recv_blockno()
{
    unsigned char x[4] = {0x00, 0x00, 0x00, 0x00};

    s100spi_recv_buffer(x, 4);

    return x[3] << 24 | x[2] << 16 | x[1] << 8 | x[0];
}

void virtualDevice::reset()
{
    Debug_printf("No Reset implemented for device %u\n", _devnum);
}

void virtualDevice::s100spi_response_ack()
{
}

void virtualDevice::s100spi_response_nack()
{
}

void virtualDevice::s100spi_control_ready()
{
    s100spi_response_ack();
}

void systemBus::wait_for_idle()
{
}

void virtualDevice::s100spi_process(uint8_t b)
{
    fnUartDebug.printf("s100spi_process() not implemented yet for this device. Cmd received: %02x\n", b);
}

void virtualDevice::s100spi_control_status()
{
}

void virtualDevice::s100spi_response_status()
{
}

void virtualDevice::s100spi_idle()
{
    // Not implemented in base class
}

//void virtualDevice::s100spi_status()
//{
//    fnUartDebug.printf("s100spi_status() not implemented yet for this device.\n");
//}

void systemBus::_s100spi_process_cmd()
{
}

void systemBus::_s100spi_process_queue()
{
}

void systemBus::service()
{
}

void systemBus::setup()
{
    Debug_println("s100spi SETUP");

}

void systemBus::shutdown()
{
    for (auto devicep : _daisyChain)
    {
        Debug_printf("Shutting down device %02x\n", devicep.second->id());
        devicep.second->shutdown();
    }
    Debug_printf("All devices shut down.\n");
}

void systemBus::addDevice(virtualDevice *pDevice, uint8_t device_id)
{
    Debug_printf("Adding device: %02X\n", device_id);
    pDevice->_devnum = device_id;
    _daisyChain[device_id] = pDevice;

    switch (device_id)
    {
    case 0x02:
        _printerDev = (s100spiPrinter *)pDevice;
        break;
    case 0x0f:
        _fujiDev = (s100spiFuji *)pDevice;
        break;
    }
}

bool systemBus::deviceExists(uint8_t device_id)
{
    return _daisyChain.find(device_id) != _daisyChain.end();
}

void systemBus::remDevice(virtualDevice *pDevice)
{

}

void systemBus::remDevice(uint8_t device_id)
{
    if (deviceExists(device_id))
    {
        _daisyChain.erase(device_id);
    }
}

int systemBus::numDevices()
{
    return _daisyChain.size();
}

void systemBus::changeDeviceId(virtualDevice *p, uint8_t device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep.second == p)
            devicep.second->_devnum = device_id;
    }
}

virtualDevice *systemBus::deviceById(uint8_t device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep.second->_devnum == device_id)
            return devicep.second;
    }
    return nullptr;
}

void systemBus::reset()
{
    for (auto devicep : _daisyChain)
        devicep.second->reset();
}

void systemBus::enableDevice(uint8_t device_id)
{
    if (_daisyChain.find(device_id) != _daisyChain.end())
        _daisyChain[device_id]->device_active = true;
}

void systemBus::disableDevice(uint8_t device_id)
{
    if (_daisyChain.find(device_id) != _daisyChain.end())
        _daisyChain[device_id]->device_active = false;
}

systemBus s100Bus;
#endif /* NEW_TARGET */