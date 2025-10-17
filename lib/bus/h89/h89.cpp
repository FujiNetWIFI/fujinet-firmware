#ifdef BUILD_H89

/**
 * H89 Functions
 */
#include "h89.h"

#include "../../include/debug.h"
#include "driver/spi_slave.h"


#include "fnConfig.h"
#include "fnSystem.h"

#include "led.h"
#include "modem.h" 



void virtualDevice::process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;


    fnUartDebug.printf("process() not implemented yet for this device. Cmd received: %02x\n", cmdFrame.comnd);
}

void systemBus::service()
{
    // Listen to the bus, and react here.
}
    
void systemBus::setup()
{
    Debug_println("H89 SETUP");

    // // Do any first time setup here
    // fnSystem.set_pin_mode(PIN_CMD, gpio_mode_t::GPIO_MODE_INPUT); // There's no PULLUP/PULLDOWN on pins 34-39
    // fnSystem.set_pin_mode(PIN_DATA, gpio_mode_t::GPIO_MODE_INPUT); // There's no PULLUP/PULLDOWN on pins 34-39

    // fnSystem.set_pin_mode(PIN_CMD_RDY, gpio_mode_t::GPIO_MODE_OUTPUT);
    // fnSystem.digital_write(PIN_CMD_RDY, DIGI_HIGH);

    // fnSystem.set_pin_mode(PIN_PROCEED, gpio_mode_t::GPIO_MODE_OUTPUT);
    // fnSystem.digital_write(PIN_PROCEED, DIGI_HIGH);

    // // Set up SPI bus
    // spi_bus_config_t bus_cfg = 
    // {
    //     .mosi_io_num = PIN_BUS_DEVICE_MOSI,
    //     .miso_io_num = PIN_BUS_DEVICE_MISO,
    //     .sclk_io_num = PIN_BUS_DEVICE_SCK,
    //     .quadwp_io_num = -1,
    //     .quadhd_io_num = -1,
    //     .max_transfer_sz = 4096,
    //     .flags=0,
    //     .intr_flags=0,
    // };

    // spi_slave_interface_config_t slave_cfg =
    // {
    //     .spics_io_num=PIN_BUS_DEVICE_CS,
    //     .flags=0,
    //     .queue_size=1,
    //     .mode=0,
    //     .post_setup_cb=my_post_setup_cb,
    //     .post_trans_cb=my_post_trans_cb
    // };

    // esp_err_t rc = spi_slave_initialize(RC2014_SPI_HOST, &bus_cfg, &slave_cfg, SPI_DMA_DISABLED);
    // if (rc != ESP_OK) {
    //     Debug_println("RC2014 unable to initialise bus SPI Flush");
    // }

    // // Create a message queue
    // //qRs232Messages = xQueueCreate(4, sizeof(rs232_message_t));

    // Debug_println("RC2014 Setup Flush");
}

void systemBus::shutdown()
{
    shuttingDown = true;

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

bool systemBus::enabledDeviceStatus(uint8_t device_id)
{
    if (_daisyChain.find(device_id) != _daisyChain.end())
        return _daisyChain[device_id]->device_active;

    return false;
}
#endif /* H89 */
