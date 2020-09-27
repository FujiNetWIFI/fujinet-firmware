/**
 * N: Firmware
*/
#include <string.h>
#include <algorithm>
#include "network.h"
#include "networkProtocolTCP.h"
#include "networkProtocolUDP.h"
#include "networkProtocolHTTP.h"
#include "networkProtocolTNFS.h"
#include "networkProtocolFTP.h"

using namespace std;

/** SIO COMMANDS ***************************************************************/

/**
 * Process incoming SIO command for device 0x7X
 * @param comanddata incoming 4 bytes containing command and aux bytes
 * @param checksum 8 bit checksum
 */
void sioNetwork::sio_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    Debug_printf("sioNetwork::sio_process 0x%02hx '%c': 0x%02hx, 0x%02hx\n",
                 cmdFrame.comnd, cmdFrame.comnd, cmdFrame.aux1, cmdFrame.aux2);

    switch (cmdFrame.comnd)
    {
    case 0x3F:
        sio_ack();
        sio_high_speed();
        break;
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

/** PRIVATE METHODS ************************************************************/

/**
 * Allocate rx and tx buffers
 * @return bool TRUE if ok, FALSE if in error.
 */
bool sioNetwork::allocate_buffers()
{
#ifdef BOARD_HAS_PSRAM
    rx_buf = (uint8_t *)heap_caps_malloc(INPUT_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    tx_buf = (uint8_t *)heap_caps_malloc(OUTPUT_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    rx_buf = (uint8_t *)calloc(1, INPUT_BUFFER_SIZE);
    tx_buf = (uint8_t *)calloc(1, OUTPUT_BUFFER_SIZE);
#endif

    if ((rx_buf == nullptr) || (tx_buf == nullptr))
        return false; // Allocation failed.

    /* Clear buffer and status */
    status.reset();
    memset(rx_buf, 0, INPUT_BUFFER_SIZE);
    memset(tx_buf, 0, OUTPUT_BUFFER_SIZE);

    HEAP_CHECK("sioNetwork::allocate_buffers");
    return true; // All good.
}

/**
 * Free the rx and tx buffers
 */
void sioNetwork::free_buffers()
{
    if (rx_buf != nullptr)
        free(rx_buf);
    if (tx_buf != nullptr)
        free(tx_buf);

    Debug_printf("sioNetworks::free_buffers()\n");
}

/**
 * Instantiate protocol object
 * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
 */
bool sioNetwork::open_protocol()
{
    if (urlParser == nullptr)
    {
        Debug_printf("sioNetwork::open_protocol() - urlParser is NULL. Aborting.\n");
        return false; // error.
    }

    // Convert to uppercase
    transform(urlParser->scheme.begin(), urlParser->scheme.end(), urlParser->scheme.begin(), ::toupper);

    if (urlParser->scheme == "TCP")
    {
        protocol = new networkProtocolTCP();
    }
    else if (urlParser->scheme == "UDP")
    {
        protocol = new networkProtocolUDP();
        // TODO: Change NetworkProtocolUDP to pass saved RX buffer into ctor!
    }
    else if (urlParser->scheme == "HTTP" || urlParser->scheme == "HTTPS")
    {
        protocol = new networkProtocolHTTP();
    }
    else if (urlParser->scheme == "TNFS")
    {
        protocol = new networkProtocolTNFS();
    }
    else if (urlParser->scheme == "FTP")
    {
        protocol = new networkProtocolFTP();
    }
    else
    {
        return false; // invalid protocol.
    }

    if (protocol == nullptr)
    {
        Debug_printf("sioNetwork::open_protocol() - Could not open protocol.\n");
        return false;
    }

    Debug_printf("sioNetwork::open_protocol() - Protocol %s opened.\n", urlParser->scheme.c_str());
    return true;
}
