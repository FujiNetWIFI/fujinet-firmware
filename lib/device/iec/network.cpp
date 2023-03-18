#ifdef BUILD_IEC
/**
 * N: Firmware
 */

#include "network.h"

#include <cstring>
#include <algorithm>

#include "../../include/debug.h"
#include "../../hardware/led.h"

#include "utils.h"

#include "status_error_codes.h"
#include "TCP.h"
#include "UDP.h"
#include "Test.h"
#include "Telnet.h"
#include "TNFS.h"
#include "FTP.h"
#include "HTTP.h"
#include "SSH.h"
#include "SMB.h"

iecNetwork::iecNetwork()
{
    Debug_printf("iwmNetwork::iwmNetwork()\n");
    receiveBuffer = new string();
    transmitBuffer = new string();
    specialBuffer = new string();

    receiveBuffer->clear();
    transmitBuffer->clear();
    specialBuffer->clear();
}

iecNetwork::~iecNetwork()
{
    Debug_printf("iwmNetwork::~iwmNetwork()\n");
    receiveBuffer->clear();
    transmitBuffer->clear();
    specialBuffer->clear();

    if (receiveBuffer != nullptr)
        delete receiveBuffer;
    if (transmitBuffer != nullptr)
        delete transmitBuffer;
    if (specialBuffer != nullptr)
        delete specialBuffer;
}

void iecNetwork::shutdown()
{
    // TODO: implement.
}

void iecNetwork::process_load()
{
    switch (commanddata->secondary)
    {
    case IEC_OPEN:
    case IEC_CLOSE:
    case IEC_REOPEN: // Data
    default:
        Debug_printf("Uncaught command.\n");
        break;
    }
}

void iecNetwork::process_save()
{
    switch (commanddata->secondary)
    {
    case IEC_OPEN:
    case IEC_CLOSE:
    case IEC_REOPEN: // Data
    default:
        Debug_printf("Uncaught command.\n");
        break;
    }
}

void iecNetwork::process_command()
{
    // Add commands
}

void iecNetwork::process_channel()
{
    switch (commanddata->secondary)
    {
    case IEC_OPEN:
    case IEC_CLOSE:
    case IEC_REOPEN: // Data
    default:
        Debug_printf("Uncaught command.\n");
        break;
    }
}

device_state_t iecNetwork::process(IECData *id)
{
    // Call base class
    virtualDevice::process(id);

    // fan out to appropriate process routine
    switch (commanddata->channel)
    {
    case 1:
        process_load();
        break;
    case 2:
        process_save();
        break;
    case 15:
        process_command();
        break;
    default:
        process_channel();
        break;
    }

    return device_state;
}

#endif