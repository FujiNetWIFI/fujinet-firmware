#include "network.h"

/**
 * Parse deviceSpecs of the format
 * Nx:PROTO:PATH:PORT or
 * Nx:PROTO:PORT
 */
bool sioNetwork::parse_deviceSpec(char *tmp)
{

    char *p;
    char i = 0;
    char d = 0;

    p = strtok(tmp, ":"); // Get Device spec

    if (p[0] != 'N')
        return false;
    else
        strcpy(deviceSpec.device, p);

    while (p != NULL)
    {
        i++;
        p = strtok(NULL, ":");
        switch (i)
        {
        case 1:
            strcpy(deviceSpec.protocol, p);
            break;
        case 2:
            for (d = 0; d < strlen(p); d++)
                if (!isdigit(p[d]))
                {
                    strcpy(deviceSpec.path, p);
                    break;
                }
            deviceSpec.port = atoi(p);
            return true;
        case 3:
            deviceSpec.port = atoi(p);
            return true;
            break;
        default:
            return false; // Too many parameters.
        }
    }
}

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

void sioNetwork::open()
{
    char inp[256];
    byte ck = sio_to_peripheral((byte *)&inp, sizeof(inp));

    if (parse_deviceSpec(inp) == false)
    {
        openStatus.errorCode = OPEN_STATUS_INVALID_DEVICESPEC;
        openStatus.reserved1 = openStatus.reserved2 = openStatus.reserved3 = 0;
        sio_complete();
        return;
    }

    if (allocate_buffers() == false)
    {
        openStatus.errorCode = OPEN_STATUS_DEVICE_ERROR;
        openStatus.reserved1 = openStatus.reserved2 = openStatus.reserved3 = 0;
        sio_error();
    }
}

void sioNetwork::close()
{
    sio_complete();
}

void sioNetwork::read()
{
    sio_to_computer(rx_buf, sio_get_aux(), err);
}

void sioNetwork::write()
{
    ck=sio_to_peripheral(tx_buf,sio_get_aux());
}

void sioNetwork::status()
{
}
