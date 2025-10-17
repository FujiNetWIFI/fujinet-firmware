#ifdef BUILD_H89

/**
 * H89 Functions
 */
#include <bitset>
 #include "h89.h"

#include "../../include/debug.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_struct.h"


#include "fnConfig.h"
#include "fnSystem.h"

#include "led.h"
#include "modem.h" 

#include "driver/gpio.h"

#define ACK_DELAY_US 1

unsigned char xbyte=0xAA;

#define D0 GPIO_NUM_32
#define D1 GPIO_NUM_33
#define D2 GPIO_NUM_25
#define D3 GPIO_NUM_26
#define D4 GPIO_NUM_27
#define D5 GPIO_NUM_14
#define D6 GPIO_NUM_12
#define D7 GPIO_NUM_13

#define STB GPIO_NUM_4
#define ACK GPIO_NUM_15
#define IBF GPIO_NUM_34
#define OBF GPIO_NUM_35

void virtualDevice::process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;


    fnUartDebug.printf("process() not implemented yet for this device. Cmd received: %02x\n", cmdFrame.comnd);
}

#define ACK GPIO_NUM_15

void IRAM_ATTR quick_delay()
{
    asm volatile("nop");
    asm volatile("nop");
    asm volatile("nop");
    asm volatile("nop");
    asm volatile("nop");
}

void systemBus::service()
{
    printf("Set byte to: 0x%02X\r\n",xbyte);

    // This is the transmit routine
    gpio_set_level(D7,xbyte & 0x80 ? DIGI_HIGH : DIGI_LOW);
    gpio_set_level(D6,xbyte & 0x40 ? DIGI_HIGH : DIGI_LOW);
    gpio_set_level(D5,xbyte & 0x20 ? DIGI_HIGH : DIGI_LOW);
    gpio_set_level(D4,xbyte & 0x10 ? DIGI_HIGH : DIGI_LOW);
    gpio_set_level(D3,xbyte & 0x08 ? DIGI_HIGH : DIGI_LOW);
    gpio_set_level(D2,xbyte & 0x04 ? DIGI_HIGH : DIGI_LOW);
    gpio_set_level(D1,xbyte & 0x02 ? DIGI_HIGH : DIGI_LOW);
    gpio_set_level(D0,xbyte & 0x01 ? DIGI_HIGH : DIGI_LOW);


    // set STB to low
    printf("Set STB To low \r\n");
    gpio_set_level(STB,DIGI_LOW);

    // Wait for IBF to go HIGH
    printf("Wait for IBF to go HIGH\r\n");
    while(gpio_get_level(IBF) == DIGI_LOW);

    // set STB to high
    printf("Set STB to HIGH\r\n");
    gpio_set_level(STB,DIGI_HIGH);

    // Wait for IBF to go LOW
    printf("Wait for IBF to go LOW\r\n");
    while(gpio_get_level(IBF) == DIGI_HIGH);

    // Increment xbyte and go again...
    printf("Increment xbyte and go again...\r\n");
    xbyte++; 


    ////////////////////////////////////////////////////////////////////////////
    // THIS IS THE RECEIVE ROUTINE. IT WORKS!
    // /STB is set high in ::setup()

    // Wait for /OBF to go high
    Debug_printf("Wait for OBF to go high.\r\n");
    while (gpio_get_level(GPIO_NUM_35) == DIGI_HIGH);

    Debug_printf("OBF Low\n Setting ACK LOW\r\n");

    // Set /ACK low.
    gpio_set_level(ACK,DIGI_LOW);
    quick_delay();

    Debug_printf("Waited 1us, sampling D0-D7\r\n");

    // sample and output bus state on console.
    printf("LOW: ");
    printf("%u",gpio_get_level(GPIO_NUM_13));
    printf("%u",gpio_get_level(GPIO_NUM_12));
    printf("%u",gpio_get_level(GPIO_NUM_14));
    printf("%u",gpio_get_level(GPIO_NUM_27));
    printf("%u",gpio_get_level(GPIO_NUM_26));
    printf("%u",gpio_get_level(GPIO_NUM_25));
    printf("%u",gpio_get_level(GPIO_NUM_33));
    printf("%u\n",gpio_get_level(GPIO_NUM_32));

    Debug_printf("Setting /ACK high again.\r\n");

    // Set /ACK high again.
    gpio_set_level(ACK,DIGI_HIGH);
    quick_delay();

    Debug_printf("Waiting for OBF to go high\r\n");

    // Wait for /OBF to go low
    while (gpio_get_level(GPIO_NUM_35) == DIGI_LOW);
    
    Debug_printf("OBF now high. Sampling bus.\r\n");

    // sample and output bus state on console.
    printf(" HI: ");
    printf("%u",gpio_get_level(GPIO_NUM_13));
    printf("%u",gpio_get_level(GPIO_NUM_12));
    printf("%u",gpio_get_level(GPIO_NUM_14));
    printf("%u",gpio_get_level(GPIO_NUM_27));
    printf("%u",gpio_get_level(GPIO_NUM_26));
    printf("%u",gpio_get_level(GPIO_NUM_25));
    printf("%u",gpio_get_level(GPIO_NUM_33));
    printf("%u\n",gpio_get_level(GPIO_NUM_32));

    Debug_printf("Looping back around...\r\n");
}
    
void systemBus::setup()
{
    Debug_println("H89 SETUP");

    // // Reset pins
    gpio_reset_pin(GPIO_NUM_0);
    gpio_reset_pin(GPIO_NUM_2);
    gpio_reset_pin(GPIO_NUM_4);
    gpio_reset_pin(GPIO_NUM_12);
    gpio_reset_pin(GPIO_NUM_13);
    gpio_reset_pin(GPIO_NUM_14);
    gpio_reset_pin(GPIO_NUM_15);
    gpio_reset_pin(GPIO_NUM_22);
    gpio_reset_pin(GPIO_NUM_25);
    gpio_reset_pin(GPIO_NUM_26);
    gpio_reset_pin(GPIO_NUM_27);
    gpio_reset_pin(GPIO_NUM_32);
    gpio_reset_pin(GPIO_NUM_33);
    gpio_reset_pin(GPIO_NUM_34);

    gpio_set_direction(GPIO_NUM_0,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(GPIO_NUM_2,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(GPIO_NUM_4,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(GPIO_NUM_12,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(GPIO_NUM_13,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(GPIO_NUM_14,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(GPIO_NUM_15,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(GPIO_NUM_22,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(GPIO_NUM_25,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(GPIO_NUM_26,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(GPIO_NUM_27,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(GPIO_NUM_32,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(GPIO_NUM_33,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(GPIO_NUM_34,GPIO_MODE_INPUT);
    gpio_set_direction(GPIO_NUM_35,GPIO_MODE_INPUT);

    Debug_printf("Setting GPIO #15 (/ACK) to high.");
    gpio_set_level(GPIO_NUM_15,DIGI_HIGH);

    gpio_set_level(GPIO_NUM_4,DIGI_HIGH);
    Debug_printf("GPIO #4 (/STB) SET HIGH.\n");

    Debug_printf("GPIO CONFIGURED\n");
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

systemBus H89Bus;
#endif /* H89 */
