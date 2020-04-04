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

    if ((rx_buf == nullptr) || (tx_buf == nullptr) || (sp_buf == nullptr))
    {
        return false;
    }
    else
    {
        memset(rx_buf, 0, INPUT_BUFFER_SIZE);
        memset(tx_buf, 0, OUTPUT_BUFFER_SIZE);
        memset(sp_buf, 0, SPECIAL_BUFFER_SIZE);
        return true;
    }
}

/**
 * Deallocate input/output buffers
 */
void sioNetwork::deallocate_buffers()
{
    if (rx_buf != nullptr)
        free(rx_buf);

    if (tx_buf != nullptr)
        free(tx_buf);

    if (sp_buf != nullptr)
        free(sp_buf);
}

bool sioNetwork::open_protocol()
{
    if (strcmp(deviceSpec.protocol, "TCP") == 0)
    {
        protocol = new networkProtocolTCP();
        return true;
    }
    else
    {
        return false;
    }        
}

void sioNetwork::sio_open()
{
    char inp[256];

    sio_ack();

    deviceSpec.clear();
    memset(&inp,0,sizeof(inp));
    memset(&status_buf.rawData,0,sizeof(status_buf.rawData));

    sio_to_peripheral((byte *)&inp, sizeof(inp));

#ifdef DEBUG
    Debug_printf("Open: %s\n", inp);
#endif

    if (deviceSpec.parse(inp) == false)
    {
#ifdef DEBUG
        Debug_printf("Invalid devicespec\n");
#endif
        status_buf.error = 165;
        sio_error();
        return;
    }

    if (allocate_buffers() == false)
    {
#ifdef DEBUG
        Debug_printf("Could not allocate memory for buffers\n");
#endif
        status_buf.error = 129;
        sio_error();
        return;
    }

    if (open_protocol() == false)
    {
#ifdef DEBUG
        Debug_printf("Could not open protocol.\n");
#endif
        status_buf.error = 128;
        sio_error();
        return;     
    }

    if (!protocol->open(&deviceSpec))
    {
#ifdef DEBUG
        Debug_printf("Protocol unable to make connection.");
#endif
        status_buf.error = 170;
    }

    sio_complete();
}

void sioNetwork::sio_close()
{
#ifdef DEBUG
    Debug_printf("Close.\n");
#endif
    sio_ack();

    if (protocol == nullptr)
    {
        sio_complete();
        return;
    }

    if (protocol->close())
        sio_complete();
    else
        sio_error();

    delete protocol;
    protocol = nullptr;

    status_buf.error=0; // clear error

    deallocate_buffers();
}

void sioNetwork::sio_read()
{
    sio_ack();
#ifdef DEBUG
    Debug_printf("Read %d bytes\n", cmdFrame.aux2 * 256 + cmdFrame.aux1);
#endif
    if (protocol == nullptr)
    {
        err = true;
        status_buf.error = 128;
    }
    else
    {
        err = protocol->read(rx_buf, cmdFrame.aux2 * 256 + cmdFrame.aux1);
    }
    sio_to_computer(rx_buf, sio_get_aux(), err);
}

void sioNetwork::sio_write()
{
#ifdef DEBUG
    Debug_printf("Write %d bytes\n", cmdFrame.aux2 * 256 + cmdFrame.aux1);
#endif

    sio_ack();

    if (protocol == nullptr)
    {
#ifdef DEBUG
        Debug_printf("Not connected\n");
#endif
        err = true;
        status_buf.error = 128;
        sio_error();
    }
    else
    {
        ck = sio_to_peripheral(tx_buf, sio_get_aux());
        tx_buf_len = cmdFrame.aux2 * 256 + cmdFrame.aux1;

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
    if (!protocol)
    {
        status_buf.rawData[0] =
            status_buf.rawData[1] = 0;

        status_buf.rawData[2] = WiFi.isConnected();
        err = false;
    }
    else
    {
        err = protocol->status(status_buf.rawData);
    }
#ifdef DEBUG
    Debug_printf("Status bytes: %02x %02x %02x %02x\n", status_buf.rawData[0], status_buf.rawData[1], status_buf.rawData[2], status_buf.rawData[3]);
#endif
    sio_to_computer(status_buf.rawData, 4, err);
}

void sioNetwork::sio_special()
{
    sio_ack();
#ifdef DEBUG
    Debug_printf("SPECIAL\n");
#endif
    if (protocol == nullptr)
    {
#ifdef DEBUG
        Debug_printf("Not connected!\n");
#endif
        err = true;
        status_buf.error = OPEN_STATUS_NOT_CONNECTED;
    }
    else
    {
        protocol->special(sp_buf, sp_buf_len, &cmdFrame);
    }
}

void sioNetwork::sio_process()
{
    switch (cmdFrame.comnd)
    {
    case 'O':
        sio_open();
        break;
    case 'C':
        sio_close();
        break;
    case 'R':
        sio_read();
        break;
    case 'W':
        sio_write();
        break;
    case 'S':
        sio_status();
        break;
    default:
        sio_special();
        break;
    }
}