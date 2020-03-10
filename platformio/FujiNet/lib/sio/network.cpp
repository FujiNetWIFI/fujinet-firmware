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
    
    sio_ack();

    sio_to_peripheral((byte *)&inp, sizeof(inp));

#ifdef DEBUG
    Debug_printf("Open: %s\n",inp);
#endif

    if (deviceSpec.parse(inp) == false)
    {
#ifdef DEBUG
        Debug_printf("Invalid devicespec\n");
#endif
        memset(&status_buf, 0, sizeof(status_buf.rawData));
        status_buf.error = OPEN_STATUS_INVALID_DEVICESPEC;
        sio_complete();
        return;
    }

    if (allocate_buffers() == false)
    {
#ifdef DEBUG
        Debug_printf("Could not allocate memory for buffers\n");
#endif
        memset(&status_buf, 0, sizeof(status_buf.rawData));
        status_buf.error = OPEN_STATUS_DEVICE_ERROR;
        sio_error();
    }

    if (open_protocol() == false)
    {
#ifdef DEBUG
        Debug_printf("Could not open protocol.\n");
#endif        
        memset(&status_buf, 0, sizeof(status_buf.rawData));
        status_buf.error = OPEN_STATUS_NOT_CONNECTED;
        sio_error();
    }
}

void sioNetwork::close()
{
#ifdef DEBUG
    Debug_printf("Close.\n");
#endif
    sio_ack();
    if (protocol->close())
        sio_complete();
    else
        sio_error();
}

void sioNetwork::read()
{
    sio_ack();
#ifdef DEBUG
    Debug_printf("Read %d bytes\n",cmdFrame.aux2*256+cmdFrame.aux1);
#endif
    if (protocol == NULL)
    {
        err = true;
        status_buf.error = OPEN_STATUS_NOT_CONNECTED;
    }
    else
    {
        err = protocol->read(rx_buf, cmdFrame.aux2*256+cmdFrame.aux1);
    }
    sio_to_computer(rx_buf, sio_get_aux(), err);
}

void sioNetwork::write()
{
    sio_ack();
#ifdef DEBUG
    Debug_printf("Write %d bytes\n",cmdFrame.aux2*256+cmdFrame.aux1);
#endif
    ck = sio_to_peripheral(tx_buf, sio_get_aux());
    if (protocol == NULL)
    {
#ifdef DEBUG
        Debug_printf("Not connected\n");
#endif
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

void sioNetwork::sio_status()
{
    sio_ack();
#ifdef DEBUG
    Debug_printf("STATUS\n");
#endif
    if (protocol == NULL)
    {
#ifdef DEBUG
        Debug_printf("Not connected\n");
#endif
        err = true;
        status_buf.error = OPEN_STATUS_NOT_CONNECTED;
    }
    else
    {
        err = protocol->status(status_buf.rawData);
    }
#ifdef DEBUG
        Debug_printf("Status bytes: %02x %02x %02x %02x\n",status_buf.rawData[0],status_buf.rawData[1],status_buf.rawData[2],status_buf.rawData[3]);
#endif
    sio_to_computer(status_buf.rawData, 4, err);
}

void sioNetwork::special()
{
    sio_ack();
#ifdef DEBUG
    Debug_printf("SPECIAL\n");
#endif
    if (protocol == NULL)
    {
#ifdef DEBUG
        Debug_printf("Not connected!\n");
#endif
        err = true;
        status_buf.error = OPEN_STATUS_NOT_CONNECTED;
    }
    else
    {
        protocol->special(sp_buf,sp_buf_len, &cmdFrame);
    }
}

void sioNetwork::sio_process()
{
    switch (cmdFrame.comnd)
    {
        case 'O':
            open();
            break;
        case 'C':
            close();
            break;
        case 'R':
            read();
            break;
        case 'W':
            write();
            break;
        case 'S':
            sio_status();
            break;
        default:
            special();
            break;
    }
}