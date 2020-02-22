#include "network.h"
#include "networkProtocol.h"
#include "networkProtocolTCP.h"

/**
 * Allocate input/output buffers
 */
bool sioNetwork::allocate_buffers()
{
    rx_buf = (byte *)malloc(INPUT_BUFFER_SIZE);
    tx_buf = (byte *)malloc(OUTPUT_BUFFER_SIZE);
    sp_buf = (byte *)malloc(SPECIAL_BUFFER_SIZE);

    if ((rx_buf == NULL) || (tx_buf == NULL) || (sp_buf == NULL))
        return false;
    else
        return true;
}

bool sioNetwork::open_protocol()
{
    if (strcmp(deviceSpec.protocol, "TCP") == 0)
        protocol = new networkProtocolTCP();

    return protocol->open(&deviceSpec);
}

void sioNetwork::open()
{
    char inp[256];
    byte ck = sio_to_peripheral((byte *)&inp, sizeof(inp));

    if (deviceSpec.parse(inp) == false)
    {
        memset(&status_buf, 0, sizeof(status_buf.rawData));
        status_buf.error = OPEN_STATUS_INVALID_DEVICESPEC;
        sio_complete();
        return;
    }

    if (allocate_buffers() == false)
    {
        memset(&status_buf, 0, sizeof(status_buf.rawData));
        status_buf.error = OPEN_STATUS_DEVICE_ERROR;
        sio_error();
    }

    if (open_protocol() == false)
    {
        memset(&status_buf, 0, sizeof(status_buf.rawData));
        status_buf.error = OPEN_STATUS_NOT_CONNECTED;
        sio_error();
    }
}

void sioNetwork::close()
{
    if (protocol->close())
        sio_complete();
    else
        sio_error();
}

void sioNetwork::read()
{
    if (protocol == NULL)
    {
        err = true;
        status_buf.error = OPEN_STATUS_NOT_CONNECTED;
    }
    else
    {
        err = protocol->read(rx_buf, rx_buf_len);
    }
    sio_to_computer(rx_buf, sio_get_aux(), err);
}

void sioNetwork::write()
{
    ck = sio_to_peripheral(tx_buf, sio_get_aux());
    if (protocol == NULL)
    {
        err = true;
        status_buf.error = OPEN_STATUS_NOT_CONNECTED;
    }
    else
    {
        if (protocol->write(tx_buf, tx_buf_len))
        {
            sio_complete();
        }
        else
        {
            sio_error();
        }
    }
}

void sioNetwork::status()
{
    if (protocol == NULL)
    {
        err = true;
        status_buf.error = OPEN_STATUS_NOT_CONNECTED;
    }
    else
    {
        err = protocol->status(status_buf.rawData);
    }
    sio_to_computer(status_buf.rawData, 4, err);
}

void sioNetwork::special(char dstats)
{
    if (protocol == NULL)
    {
        err = true;
        status_buf.error = OPEN_STATUS_NOT_CONNECTED;
    }
    else
    {
        switch (dstats)
        {
        case 0x40:
            sio_to_computer(rx_buf, rx_buf_len, err);
            break;
        case 0x80:
            ck = sio_to_peripheral(tx_buf, tx_buf_len);

            if (sio_checksum(tx_buf, tx_buf_len) == ck)
                if (protocol->special(tx_buf, tx_buf_len) == true)
                    sio_complete();
                else
                    sio_error();

            break;
        }
    }
}