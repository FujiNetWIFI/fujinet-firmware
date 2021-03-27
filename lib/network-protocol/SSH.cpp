/**
 * SSH protocol implementation
 */

#include "SSH.h"
#include "status_error_codes.h"

NetworkProtocolSSH::NetworkProtocolSSH(string *rx_buf, string *tx_buf, string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolSSH::NetworkProtocolSSH(%p,%p,%p)\n", rx_buf, tx_buf, sp_buf);
}

NetworkProtocolSSH::~NetworkProtocolSSH()
{
    Debug_printf("NetworkProtocolSSH::~NetworkProtocolSSH()\n");
}

bool NetworkProtocolSSH::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    NetworkProtocol::open(urlParser, cmdFrame);
    int ret;

    if ((ret = libssh2_init(0)) != 0)
    {
        Debug_printf("NetworkProtocolSSH::open() - libssh2_init not successful. Value returned: %d\n",ret);
        error = NETWORK_ERROR_GENERAL;
        return true;
    }

    if (client.connect(urlParser->hostName.c_str(),atoi(urlParser->port.c_str())) == 0)
    {
        Debug_printf("NetworkProtocolSSH::open() - Could not connect to host. Aborting.\n");
        return true;
    }

    Debug_printf("NetworkProtocolSSH::open() - Opening session.");
    session = libssh2_session_init();
    if (session == nullptr)
    {
        Debug_printf("Could not create session. aborting.\n");
        return true;
    }

    Debug_printf("NetworkProtocolSSH::open() - Attempting session handshake with fd %u\n",client.fd());
    if (libssh2_session_handshake(session, client.fd()))
    {
        error = NETWORK_ERROR_GENERAL;
        Debug_printf("NetworkProtocolSSH::open() - Could not perform SSH handshake.\n");
        return true;
    }

    return false;
}

bool NetworkProtocolSSH::close()
{
    libssh2_session_disconnect(session, "Closed by NetworkProtocolSSH::close()");
    libssh2_session_free(session);
    libssh2_exit();
    return false;
}

bool NetworkProtocolSSH::read(unsigned short len)
{
    return NetworkProtocol::read(len);
}

bool NetworkProtocolSSH::write(unsigned short len)
{
    bool err = false;

    return err;
}

bool NetworkProtocolSSH::status(NetworkStatus *status)
{
    NetworkProtocol::status(status);

    return false;
}

uint8_t NetworkProtocolSSH::special_inquiry(uint8_t cmd)
{
    return 0xFF; // selected command not implemented.
}

bool NetworkProtocolSSH::special_00(cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSSH::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSSH::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}