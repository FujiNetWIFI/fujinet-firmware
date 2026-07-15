/**
 * NetworkProtocolWS
 *
 * WebSocket Protocol Adapter Implementation
 */

#include "WS.h"

#include <vector>

#include "../../include/debug.h"
#include "status_error_codes.h"

NetworkProtocolWS::NetworkProtocolWS(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolWS::ctor\r\n");
}

NetworkProtocolWS::~NetworkProtocolWS()
{
    Debug_printf("NetworkProtocolWS::dtor\r\n");
    if (client != nullptr)
    {
        delete client;
        client = nullptr;
    }
}

// Build a ws://|wss:// URL from opened_url, forcing a lowercase scheme.
std::string NetworkProtocolWS::build_url()
{
    std::string u = secure() ? "wss://" : "ws://";

    if (!opened_url->user.empty())
    {
        u += opened_url->user;
        if (!opened_url->password.empty())
            u += ":" + opened_url->password;
        u += "@";
    }

    u += opened_url->host;
    if (!opened_url->port.empty())
        u += ":" + opened_url->port;

    u += opened_url->path.empty() ? "/" : opened_url->path;

    if (!opened_url->query.empty())
        u += "?" + opened_url->query;

    return u;
}

fujiError_t NetworkProtocolWS::open(PeoplesUrlParser *urlParser, fileAccessMode_t access,
                                    netProtoTranslation_t translate)
{
    NetworkProtocol::open(urlParser, access, translate);

    if (opened_url->host.empty())
    {
        error = NDEV_STATUS::INVALID_DEVICESPEC;
        return FUJI_ERROR::UNSPECIFIED;
    }

    client = new WS_CLIENT_CLASS();

    // ?frame=text sends TEXT frames; default (or ?frame=bin) sends BINARY.
    std::string frame = opened_url->queryParam("frame");
    client->set_frame_text(frame == "text" || frame == "txt");

    std::string url = build_url();
    Debug_printf("NetworkProtocolWS::open(%s)\r\n", url.c_str());

    if (!client->connect(url))
    {
        Debug_printf("NetworkProtocolWS::open - connect failed\r\n");
        error = NDEV_STATUS::CONNECTION_REFUSED;
        return FUJI_ERROR::UNSPECIFIED;
    }

    error = NDEV_STATUS::SUCCESS;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolWS::close()
{
    Debug_printf("NetworkProtocolWS::close()\r\n");

    NetworkProtocol::close();

    if (client != nullptr)
    {
        client->stop();
        delete client;
        client = nullptr;
    }

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolWS::read(unsigned short len)
{
    Debug_printf("NetworkProtocolWS::read(%u)\r\n", len);

    if (receiveBuffer->length() == 0)
    {
        std::vector<uint8_t> newData(len);
        int actual_len = client->read(newData.data(), len);

        if (actual_len < 0 || (!client->connected() && actual_len == 0))
        {
            error = NDEV_STATUS::NOT_CONNECTED;
            return FUJI_ERROR::UNSPECIFIED;
        }

        receiveBuffer->insert(receiveBuffer->end(), newData.begin(), newData.begin() + actual_len);

        if ((unsigned short)actual_len != len)
        {
            // Short read: return what we have and flag a timeout, like TCP.
            Debug_printf("Short receive. Got %u of %u bytes.\r\n", actual_len, len);
            error = NDEV_STATUS::SOCKET_TIMEOUT;
            return FUJI_ERROR::UNSPECIFIED;
        }
    }

    error = NDEV_STATUS::SUCCESS;
    return NetworkProtocol::read(len);
}

fujiError_t NetworkProtocolWS::write(unsigned short len)
{
    Debug_printf("NetworkProtocolWS::write(%u)\r\n", len);

    if (!client->connected())
    {
        error = NDEV_STATUS::NOT_CONNECTED;
        return FUJI_ERROR::UNSPECIFIED;
    }

    // Call base class to do translation.
    len = translate_transmit_buffer();

    int actual_len = client->write((uint8_t *)transmitBuffer->data(), len);

    if (actual_len != (int)len)
    {
        Debug_printf("Short send. Sent %d of %u bytes.\r\n", actual_len, len);
        error = NDEV_STATUS::SOCKET_TIMEOUT;
        return FUJI_ERROR::UNSPECIFIED;
    }

    error = NDEV_STATUS::SUCCESS;
    transmitBuffer->erase(0, len);

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolWS::status(NetworkStatus *status)
{
    bool up = client != nullptr && client->connected();
    status->connected = up;
    status->error = up ? error : NDEV_STATUS::END_OF_FILE;

    NetworkProtocol::status(status);

    return FUJI_ERROR::NONE;
}

size_t NetworkProtocolWS::available()
{
    size_t avail = receiveBuffer->size();
    if (!avail && client != nullptr)
        avail = client->available();
    return avail;
}
