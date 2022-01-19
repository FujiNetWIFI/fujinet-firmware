#ifdef BUILD_ADAM
#include <memory.h>
#include <string.h>

#include "../../include/debug.h"
#include "../utils/utils.h"

#include "../device/adamnet/query_device.h"

// ctor
adamQueryDevice::adamQueryDevice()
{
}

// dtor
adamQueryDevice::~adamQueryDevice()
{
}

bool adamQueryDevice::adamDeviceExists(uint8_t device)
{
    uint8_t tx;
    bool again;
    bool timeout = false;
    uint8_t count = 2;
    uint8_t rxbuf[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    unsigned short bytes;
    
    AdamNet.wait_for_idle();
    tx = CONTROL_STATUS | device;
    //Debug_printf("CTRL_STATUS TX: %02x\n", tx);
    adamnet_send(tx);
    do
    {
        rxbuf[0] = 0xff;
        timeout = adamnet_recv_timeout(&rxbuf[0], 200);
        if (timeout)
        {
            break;
        }
        //Debug_printf("%02x\n", rxbuf[0]);
        again = rxbuf[0] == tx; // ignore our own message
        again |= (rxbuf[0] != (RESPONSE_STATUS | device));
        again &= (--count);
        //if (again)
        //    printf("again\n");
    } while (again);

    if (timeout)
    {
        //Debug_printf("Timeout\n");
        return false;
    } else
    {
        bytes = adamnet_recv_buffer(&rxbuf[1], 4);

        //Debug_printf("%02x [%02x%02x] %02x *%02x* [BYTES: %d]\n", rxbuf[0], rxbuf[2], rxbuf[1], rxbuf[3], rxbuf[4], (int)bytes);

        return (bytes == 4);
    }
}


// virtual functions

void adamQueryDevice::adamnet_control_ready() { }
void adamQueryDevice::shutdown() { }
void adamQueryDevice::adamnet_process(uint8_t b) { }
void adamQueryDevice::adamnet_control_status() { }
void adamQueryDevice::adamnet_control_receive()  { }
void adamQueryDevice::adamnet_control_clr() { }

#endif /* BUILD_ADAM */