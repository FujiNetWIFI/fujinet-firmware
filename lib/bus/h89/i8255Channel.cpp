#include "i8255Channel.h"
#include "fnSystem.h"
#include <driver/gpio.h>

// FIXME - move to include/pinmap

#define D0 GPIO_NUM_32
#define D1 GPIO_NUM_33
#define D2 GPIO_NUM_25
#define D3 GPIO_NUM_26
#define D4 GPIO_NUM_27
#define D5 GPIO_NUM_14
#define D6 GPIO_NUM_12
#define D7 GPIO_NUM_13

#define OE GPIO_NUM_0
#define _DIR GPIO_NUM_2
#define STB GPIO_NUM_4
#define ACK GPIO_NUM_15
#define IBF GPIO_NUM_34
#define OBF GPIO_NUM_35

void i8255Channel::begin()
{
    // Reset these pins to INPUT.
    gpio_reset_pin(OE);
    gpio_reset_pin(_DIR);
    gpio_reset_pin(STB);
    gpio_reset_pin(D6);
    gpio_reset_pin(D7);
    gpio_reset_pin(D5);
    gpio_reset_pin(ACK);
    gpio_reset_pin(D2);
    gpio_reset_pin(D3);
    gpio_reset_pin(D4);
    gpio_reset_pin(D0);
    gpio_reset_pin(D1);
    gpio_reset_pin(IBF);
    gpio_reset_pin(OBF);
    gpio_reset_pin(GPIO_NUM_22); // See if we can remove this?

    // Adjust appropriate pins to INPUT, OUTPUT, or INPUT_OUTPUT.
    gpio_set_direction(OE,GPIO_MODE_INPUT);
    gpio_set_direction(_DIR,GPIO_MODE_INPUT);
    gpio_set_direction(STB,GPIO_MODE_OUTPUT);
    gpio_set_direction(D6,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(D7,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(D5,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(ACK,GPIO_MODE_OUTPUT);
    gpio_set_direction(D2,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(D3,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(D4,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(D0,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(D1,GPIO_MODE_INPUT_OUTPUT);
    gpio_set_direction(IBF,GPIO_MODE_INPUT);
    gpio_set_direction(OBF,GPIO_MODE_INPUT);
    gpio_set_direction(GPIO_NUM_22,GPIO_MODE_INPUT_OUTPUT); // can we remove this?

    // Reset /ACK and /STB
    gpio_set_level(ACK,DIGI_HIGH);
    gpio_set_level(STB,DIGI_HIGH);

    return;
}

void i8255Channel::end()
{
    return;
}

size_t i8255Channel::dataOut(const void *buffer, size_t length)
{
    return port_putbuf(buffer, length);
}

void i8255Channel::updateFIFO()
{
    while (bus_available()) {
        int c = port_getc();
        if (c < 0)
            break;
        _fifo.push_back(c);
    }

    return;
}

void i8255Channel::flushOutput()
{
    return;
}

/* Lifted directly from what was reported as working */

/**
 * @brief Check if character ready on bus
 * @return 1 if character available, otherwise 0.
 */
int i8255Channel::bus_available()
{
    return gpio_get_level(OBF);
}

/**
 * @brief Get character, if available.
 * @return Character, or -1 if not available.
 */
int i8255Channel::port_getc()
{
    int val=0;

    if (gpio_get_level(OE))
    {
        return -1;
    }

    // Fail if nothing waiting.
    if (bus_available())
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
int i8255Channel::port_getc_timeout(uint16_t t)
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

uint16_t i8255Channel::port_getbuf(void *buf, uint16_t len, uint16_t timeout)
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
int i8255Channel::port_putc(uint8_t c)
{
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
uint16_t i8255Channel::port_putbuf(const void *buf, uint16_t len)
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
