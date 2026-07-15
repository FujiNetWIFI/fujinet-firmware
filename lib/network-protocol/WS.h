/**
 * Network Protocol implementation for WebSockets (ws://)
 */

#ifndef NETWORKPROTOCOL_WS
#define NETWORKPROTOCOL_WS

#include "Protocol.h"

#ifdef ESP_PLATFORM
#include "fnWebSocketClient.h"
#define WS_CLIENT_CLASS fnWebSocketClient
#else
#include "mgWebSocketClient.h"
#define WS_CLIENT_CLASS mgWebSocketClient
#endif

class NetworkProtocolWS : public NetworkProtocol
{
public:
    NetworkProtocolWS(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);
    virtual ~NetworkProtocolWS();

    /**
     * @brief Open a WebSocket connection using URL
     * @param urlParser The URL object passed in to open.
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t open(PeoplesUrlParser *urlParser, fileAccessMode_t access,
                     netProtoTranslation_t translate) override;

    /**
     * @brief Close the WebSocket connection.
     */
    fujiError_t close() override;

    /**
     * @brief Read len bytes into receiveBuffer.
     */
    fujiError_t read(unsigned short len) override;

    /**
     * @brief Write len bytes from transmitBuffer as one WebSocket frame.
     */
    fujiError_t write(unsigned short len) override;

    /**
     * @brief Return protocol status.
     */
    fujiError_t status(NetworkStatus *status) override;

protected:
    /**
     * The WebSocket transport client (mongoose on PC, esp_websocket_client on ESP).
     */
    WS_CLIENT_CLASS *client = nullptr;

    /**
     * @brief Whether to use TLS (wss://). Overridden by NetworkProtocolWSS.
     */
    virtual bool secure() { return false; }

    size_t available() override;

private:
    /**
     * @brief Build the ws://|wss:// URL from opened_url.
     */
    std::string build_url();
};

#endif /* NETWORKPROTOCOL_WS */
