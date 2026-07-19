/**
 * NetworkProtocolWSS
 *
 * TLS WebSocket Protocol Adapter Implementation (wss://)
 */

#include "WSS.h"

#include "../../include/debug.h"

NetworkProtocolWSS::NetworkProtocolWSS(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocolWS(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolWSS::ctor\r\n");
}

NetworkProtocolWSS::~NetworkProtocolWSS()
{
    Debug_printf("NetworkProtocolWSS::dtor\r\n");
}
