#ifdef BUILD_CX16

#include <cstring>
#include "cx16_i2c.h"
#include "../../include/debug.h"
#include "driver/i2c.h"
#include "../../include/pinmap.h"
#include "led.h"

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

bool systemBus::get_i2c(uint8_t *addr, uint8_t *val)
{
    return false;
}

void systemBus::process_cmd()
{
    cmdFrame_t tempFrame;
    uint8_t ck;

    fnLedManager.set(eLed::LED_BUS, true);

    tempFrame.device = i2c_register[0];
    tempFrame.comnd = i2c_register[1];
    tempFrame.aux1 = i2c_register[2];
    tempFrame.aux2 = i2c_register[3];
    tempFrame.cksum = i2c_register[4];

    Debug_printf("\nCF: %02x %02x %02x %02x %02x\n",
                 tempFrame.device, tempFrame.comnd, tempFrame.aux1, tempFrame.aux2, tempFrame.cksum);

    ck = cx16_checksum((uint8_t *)&tempFrame.commanddata, sizeof(tempFrame.commanddata));

    if (ck != tempFrame.cksum)
    {
        Debug_printf("INVALID CHECKSUM! Got %02X expected %02X", ck, tempFrame.cksum);
        i2c_register[5] = 'N'; // NAK
        return;
    }
    else
    {
        i2c_register[5] = 'A'; // ACK
    }

    for (auto devicep : _daisyChain)
    {
        if (tempFrame.device == devicep->_devnum)
        {
            _activeDev = devicep;
            // handle command
            _activeDev->process(tempFrame.commanddata, tempFrame.checksum);
        }
    }

    fnLedManager.set(eLed::LED_BUS, false);
}

void systemBus::process_queue()
{
    // TODO IMPLEMENT
}

void systemBus::address_read(uint8_t addr)
{
    if (addr < sizeof(i2c_register))
    {
        Debug_printf("address_read(%u) = '%02X'\n", addr, i2c_register[addr]);
        i2c_buffer[0] = i2c_register[addr];
        i2c_slave_write_buffer(i2c_slave_port, i2c_buffer, 1, 1 / portTICK_PERIOD_MS);

        if (addr == 0x00)
            process_cmd();
    }
}

void systemBus::address_write(uint8_t addr, uint8_t val)
{
    if (addr < sizeof(i2c_register))
    {
        Debug_printf("address_write(%u) = '%02X'\n", addr, val);
        i2c_register[addr] = val;
    }
}

void systemBus::service()
{
    // Get packet
    int l = i2c_slave_read_buffer(i2c_slave_port, i2c_buffer, 2, 1 / portTICK_PERIOD_MS);

    // 1 byte packet = READ
    if (l == 1)
    {
        address_read(i2c_buffer[0]);
    }
    else if (l == 2) // WRITE
    {
        address_write(i2c_buffer[0], i2c_buffer[1]);
    }

    i2c_reset_rx_fifo(i2c_slave_port);
}

void systemBus::setup()
{
    i2c_config_t conf_slave;

    conf_slave.mode = I2C_MODE_SLAVE;
    conf_slave.sda_io_num = PIN_SDA;
    conf_slave.scl_io_num = PIN_SCL;
    conf_slave.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf_slave.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf_slave.slave.slave_addr = I2C_DEVICE_ID;
    conf_slave.slave.addr_10bit_en = 0;
    conf_slave.clk_flags = 0;

    esp_err_t err = i2c_param_config(i2c_slave_port, &conf_slave);

    if (err != ESP_OK)
    {
        return;
    }

    i2c_driver_install(i2c_slave_port, conf_slave.mode, I2C_SLAVE_RX_BUF_LEN, I2C_SLAVE_TX_BUF_LEN, 0);
    Debug_printf("I²C installed on port %d as device 0x%02X\n", i2c_slave_port, I2C_DEVICE_ID);
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
        Debug_printf("Shutting down device %02x\n", devicep->id());
        devicep->shutdown();
    }
    Debug_printf("All devices shut down.\n");
}

systemBus CX16;

#endif /* BUILD_CX16 */