#ifndef MG_WEBSOCKET_CLIENT_H
#define MG_WEBSOCKET_CLIENT_H

// PC/desktop WebSocket client, backed by the vendored mongoose library.
// The ESP build uses fnWebSocketClient instead (selected in WS.h).
#ifndef ESP_PLATFORM

#include <string>
#include <cstdint>

#include "mongoose.h"
#undef mkdir

class mgWebSocketClient
{
public:
    mgWebSocketClient();
    ~mgWebSocketClient();

    // Connect and block until the WS handshake completes or timeout.
    bool connect(const std::string &url, int timeout_ms = 10000);
    bool connected();
    size_t available();                       // inbound bytes buffered
    int read(uint8_t *buf, size_t size);      // drain up to size bytes
    int write(const uint8_t *buf, size_t size); // send one frame
    void set_frame_text(bool text) { _frame_op = text ? WEBSOCKET_OP_TEXT : WEBSOCKET_OP_BINARY; }
    void stop();

private:
    static void ev_handler(struct mg_connection *c, int ev, void *ev_data);
    void load_ca();

    struct mg_mgr *_mgr = nullptr;
    struct mg_connection *_conn = nullptr;
    std::string _url;
    std::string _rx;         // received frame payloads, as a byte stream
    std::string _certStore;  // backing storage for _ca
    mg_str _ca = {nullptr, 0};
    bool _connected = false;
    bool _handshake_done = false;
    bool _closed = false;
    int _error = 0;
    int _frame_op = WEBSOCKET_OP_BINARY;
};

#endif // !ESP_PLATFORM
#endif // MG_WEBSOCKET_CLIENT_H
