/**
 * AdamNet Functions
 */

#include "adamnet.h"

uint8_t adamnet_checksum(uint8_t *buf, unsigned short len)
{
    uint8_t checksum = 0x00;

    for (unsigned short i = 0; i < len; i++)
        checksum ^= buf[i];

    return checksum;
}

void adamnet_send(uint8_t b)
{
    // Write the byte
    fnUartAdamNet.write(b);

    // Wait for byte to come back around
    while (!fnUartAdamNet.available())
        fnSystem.yield();

    // Read it, but throw it away.
    fnUartAdamNet.read();
}

void adamnet_send_buffer(uint8_t *buf, unsigned short len)
{
    for (unsigned short i = 0; i < len; i++)
        adamnet_send(buf[i]);
}

uint8_t adamnet_recv()
{
    return fnUartAdamNet.read();
}

unsigned short adamnet_recv_buffer(uint8_t *buf, unsigned short len)
{
    return fnUartAdamNet.readBytes(buf, len);
}

void adamnet_wait_for_idle()
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