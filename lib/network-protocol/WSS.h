/**
 * Network Protocol implementation for TLS WebSockets (wss://)
 *
 * Identical to NetworkProtocolWS except the transport uses TLS; the only
 * difference is the URL scheme, keyed off secure().
 */

#ifndef NETWORKPROTOCOL_WSS
#define NETWORKPROTOCOL_WSS

#include "WS.h"

class NetworkProtocolWSS : public NetworkProtocolWS
{
public:
    NetworkProtocolWSS(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);
    virtual ~NetworkProtocolWSS();

protected:
    bool secure() override { return true; }
};

#endif /* NETWORKPROTOCOL_WSS */
