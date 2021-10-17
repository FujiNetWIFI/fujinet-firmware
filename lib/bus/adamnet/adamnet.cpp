/**
 * AdamNet Functions
 */

#include "adamnet.h"
#include "../../include/debug.h"
#include "utils.h"
#include "led.h"

uint8_t adamnet_checksum(uint8_t *buf, unsigned short len)
{
    uint8_t checksum = 0x00;

    for (unsigned short i = 0; i < len; i++)
        checksum ^= buf[i];

    return checksum;
}

void adamNetDevice::adamnet_send(uint8_t b)
{
    // Write the byte
    fnUartAdamNet.write(b);
}

void adamNetDevice::adamnet_send_buffer(uint8_t *buf, unsigned short len)
{
    for (unsigned short i = 0; i < len; i++)
        adamnet_send(buf[i]);
}

uint8_t adamNetDevice::adamnet_recv()
{
    uint8_t b;

    while (!fnUartAdamNet.available())
        fnSystem.yield();

    b = fnUartAdamNet.read();

    return b;
}

uint16_t adamNetDevice::adamnet_recv_length()
{
    unsigned short s = 0;
    s = adamnet_recv() << 8;
    s |= adamnet_recv();

    return s;
}

unsigned short adamNetDevice::adamnet_recv_buffer(uint8_t *buf, unsigned short len)
{
    for (int i = 0; i < len; i++)
        buf[i] = adamnet_recv();
    return len;
}

void adamNetDevice::adamnet_wait_for_idle()
{
    bool isIdle = false;
    int64_t start, current, dur;

    do
    {
        // Wait for serial line to quiet down.
        while (fnUartAdamNet.available())
            fnUartAdamNet.read();

        start = current = esp_timer_get_time();

        while ((!fnUartAdamNet.available()) && (isIdle == false))
        {
            current = esp_timer_get_time();
            dur = current - start;
            if (dur > 150)
                isIdle = true;
        }
    } while (isIdle == false);
}

void adamNetDevice::adamnet_process(uint8_t b)
{
    fnUartDebug.printf("adamnet_process() not implemented yet for this device. Cmd received: %02x\n", b);
    adamnet_wait_for_idle();
}

void adamNetDevice::adamnet_control_status()
{
    fnUartDebug.printf("adamnet_status() not implemented yet for this device.\n");
}

void adamNetBus::_adamnet_process_cmd()
{
    uint8_t b;

    b = fnUartAdamNet.read();

    uint8_t d = b & 0x0F;

    // turn on AdamNet Indicator LED
    fnLedManager.set(eLed::LED_BUS, true);

    // Find device ID and pass control to it
    for (auto devicep : _daisyChain)
    {
        if (d == devicep->_devnum)
        {
            _activeDev = devicep;
            _activeDev->adamnet_process(b);
        }
        else
            _activeDev->adamnet_wait_for_idle();
    }

    // turn off AdamNet Indicator LED
    fnLedManager.set(eLed::LED_BUS, false);
}

void adamNetBus::_adamnet_process_queue()
{
}

void adamNetBus::service()
{
    // Process out-of-band event queue
    _adamnet_process_queue();

    // Process anything waiting.
    if (fnUartAdamNet.available())
        _adamnet_process_cmd();
}

void adamNetBus::setup()
{
    Debug_println("ADAMNET SETUP");

    // Set up UART
    fnUartAdamNet.begin(ADAMNET_BAUD);
    fnUartAdamNet.flush_input();
}

void adamNetBus::shutdown()
{
    for (auto devicep : _daisyChain)
    {
        Debug_printf("Shutting down device %02x\n", devicep->id());
        devicep->shutdown();
    }
    Debug_printf("All devices shut down.\n");
}

void adamNetBus::addDevice(adamNetDevice *pDevice, int device_id)
{
    Debug_printf("Adding device: %02X\n", device_id);
    pDevice->_devnum = device_id;
    _daisyChain.push_front(pDevice);
}

void adamNetBus::remDevice(adamNetDevice *pDevice)
{
    _daisyChain.remove(pDevice);
}

int adamNetBus::numDevices()
{
    int i = 0;
    __BEGIN_IGNORE_UNUSEDVARS
    for (auto devicep : _daisyChain)
        i++;
    return i;
    __END_IGNORE_UNUSEDVARS
}

void adamNetBus::changeDeviceId(adamNetDevice *p, int device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep == p)
            devicep->_devnum = device_id;
    }
}

adamNetDevice *adamNetBus::deviceById(int device_id)
{
    for (auto devicep : _daisyChain)
    {
        if (devicep->_devnum == device_id)
            return devicep;
    }
    return nullptr;
}

adamNetBus AdamNet;