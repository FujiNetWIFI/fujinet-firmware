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

#define D0 GPIO_NUM_32
#define D1 GPIO_NUM_33
#define D2 GPIO_NUM_25
#define D3 GPIO_NUM_26
#define D4 GPIO_NUM_27
#define D5 GPIO_NUM_14
#define D6 GPIO_NUM_12
#define D7 GPIO_NUM_13

#define OE GPIO_NUM_0 
#define DIR GPIO_NUM_2
#define STB GPIO_NUM_4
#define ACK GPIO_NUM_15
#define IBF GPIO_NUM_34
#define OBF GPIO_NUM_35

void virtualDevice::process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;


    Debug_printf("process() not implemented yet for this device. Cmd received: %02x\n", cmdFrame.comnd);
}

#define ACK GPIO_NUM_15

/**
 * @brief Check if character ready on bus
 * @return 1 if character available, otherwise 0.
 */
int systemBus::bus_available()
{
    // OBF is inverted.
    return !gpio_get_level(OBF);
}

/**
 * @brief Get character, if available.
 * @return Character, or -1 if not available.
 */
int systemBus::port_getc()
{
    int val=0;

    // Fail if nothing waiting.
    if (!bus_available())
        return -1;

    gpio_set_level(ACK,DIGI_LOW);

    val  = gpio_get_level(D0);
    val |= gpio_get_level(D1) << 1;
    val |= gpio_get_level(D2) << 2;
    val |= gpio_get_level(D3) << 3;
    val |= gpio_get_level(D4) << 4;
    val |= gpio_get_level(D5) << 5;
    val |= gpio_get_level(D6) << 6;
    val |= gpio_get_level(D7) << 7;
    
    gpio_set_level(ACK,DIGI_HIGH);

    return val;
}

/**
 * @brief Get character, with timeout
 * @param t Timeout in milliseconds
 * @return Character, or -1 if not available.
 */
int systemBus::port_getc_timeout(uint16_t t)
{
    uint64_t ut = t * 1000;

    while (esp_timer_get_time() < esp_timer_get_time() + ut)
    {
        int c = port_getc();
        if (c > -1)
            return c;
    }

    return -1;
}

uint16_t systemBus::port_getbuf(void *buf, uint16_t len, uint16_t timeout)
{
    uint16_t l = 0;
    uint8_t *p = (uint8_t *)buf;

    while (len--)
    {
        int c = port_getc_timeout(timeout);

        if (c<0)
            break;
        else
            *p++ = (uint8_t)c;

        l++;
    }

    return l;
}

/**
 * @brief Put character
 * @param c Character to send
 * @return character sent, or -1 if not able to send.
 */
int systemBus::port_putc(uint8_t c)
{
    if (gpio_get_level(OE) == DIGI_HIGH)
        return -1; // Output not enabled by computer. Abort.

    // Wait for IBF to be low.
    while(gpio_get_level(IBF) == DIGI_HIGH);

    // Set data bits
    gpio_set_level(D7,c & 0x80 ? DIGI_HIGH : DIGI_LOW);
    gpio_set_level(D6,c & 0x40 ? DIGI_HIGH : DIGI_LOW);
    gpio_set_level(D5,c & 0x20 ? DIGI_HIGH : DIGI_LOW);
    gpio_set_level(D4,c & 0x10 ? DIGI_HIGH : DIGI_LOW);
    gpio_set_level(D3,c & 0x08 ? DIGI_HIGH : DIGI_LOW);
    gpio_set_level(D2,c & 0x04 ? DIGI_HIGH : DIGI_LOW);
    gpio_set_level(D1,c & 0x02 ? DIGI_HIGH : DIGI_LOW);
    gpio_set_level(D0,c & 0x01 ? DIGI_HIGH : DIGI_LOW);

    // Strobe to trigger latch (inverted)
    gpio_set_level(STB,DIGI_LOW);

    // Wait for IBF to indicate that 8255 has accepted byte
    while(gpio_get_level(IBF) == DIGI_LOW);

    // Desert strobe
    gpio_set_level(STB,DIGI_HIGH);
    
    return c;
}

/**
 * @brief Put buffer
 * @param buf pointer to buffer to send
 * @param len Length of buffer to send
 * @return number of bytes actually sent.
 */
uint16_t systemBus::port_putbuf(const void *buf, uint16_t len)
{
    uint16_t l = 0;
    uint8_t *p = (uint8_t *)buf;

    while (len--)
    {
        int c = port_putc(*p++);

        if (c<0)
            break;

        l++;
    }
    return l;
}

void systemBus::service()
{
    const char msg[] = "THIS IS SENT FROM ESP32!\r\n";

    while(1)
    {
        port_putbuf((void *)msg,strlen(msg));
    }
}
    
void systemBus::setup()
{
    Debug_println("H89 SETUP\n");
    Debug_println("TX\n");

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

    gpio_set_direction(GPIO_NUM_0,GPIO_MODE_INPUT);
    gpio_set_direction(GPIO_NUM_2,GPIO_MODE_INPUT);
    gpio_set_direction(GPIO_NUM_4,GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_NUM_12,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(GPIO_NUM_13,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(GPIO_NUM_14,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(GPIO_NUM_15,GPIO_MODE_OUTPUT);
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
#endif /* H89 */
