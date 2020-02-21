#include "network.h"

/**
 * Allocate input/output buffers
 */
bool sioNetwork::allocate_buffers()
{
    rx_buf = (byte *)malloc(INPUT_BUFFER_SIZE);
    tx_buf = (byte *)malloc(OUTPUT_BUFFER_SIZE);

    if ((rx_buf == NULL) || (tx_buf == NULL))
        return false;
    else
        return true;
}

bool sioNetwork::open_protocol()
{
    if (strcmp(deviceSpec.protocol,"TCP")==0)
    {
        
    }
}

void sioNetwork::open()
{
    char inp[256];
    byte ck = sio_to_peripheral((byte *)&inp, sizeof(inp));

    if (deviceSpec.parse(inp)==false)
    {
        memset(&status_buf,0,sizeof(status_buf.rawData));
        status_buf.error = OPEN_STATUS_INVALID_DEVICESPEC;
        sio_complete();
        return;
    }

    if (allocate_buffers() == false)
    {
        memset(&status_buf,0,sizeof(status_buf.rawData));
        status_buf.error = OPEN_STATUS_DEVICE_ERROR;
        sio_error();
    }

    if (open_protocol()==false)
    {
        memset(&status_buf,0,sizeof(status_buf.rawData));
        status_buf.error = OPEN_STATUS_NOT_CONNECTED;
        sio_error();
    }
}

void sioNetwork::close()
{
    sio_complete();
}

void sioNetwork::read()
{
    if (protocol==NULL)
    {
        err=true;
        status_buf.error = OPEN_STATUS_NOT_CONNECTED;
    }
    else
    {
        err = protocol->read(rx_buf,rx_buf_len);
    }
    sio_to_computer(rx_buf, sio_get_aux(), err);
}

void sioNetwork::write()
{
    ck=sio_to_peripheral(tx_buf,sio_get_aux());
}

void sioNetwork::status()
{
}
