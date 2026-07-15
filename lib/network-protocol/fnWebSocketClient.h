#ifndef FN_WEBSOCKET_CLIENT_H
#define FN_WEBSOCKET_CLIENT_H

// ESP32 WebSocket client, backed by the esp_websocket_client managed component.
// The PC build uses mgWebSocketClient instead (selected in WS.h).
#ifdef ESP_PLATFORM

#include <string>
#include <mutex>
#include <atomic>
#include <cstdint>

#include "esp_websocket_client.h"

class fnWebSocketClient
{
public:
    fnWebSocketClient();
    ~fnWebSocketClient();

    // Connect and block until the WS handshake completes or timeout.
    bool connect(const std::string &url, int timeout_ms = 10000);
    bool connected();
    size_t available();                       // inbound bytes buffered
    int read(uint8_t *buf, size_t size);      // drain up to size bytes
    int write(const uint8_t *buf, size_t size); // send one frame
    void set_frame_text(bool text) { _frame_text = text; }
    void stop();

private:
    static void _event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data);

    esp_websocket_client_handle_t _client = nullptr;
    std::string _url;
    std::string _rx;      // received frame payloads, as a byte stream
    std::mutex _rx_mtx;   // guards _rx (filled from the esp client's task)
    std::atomic<bool> _connected{false};
    std::atomic<int> _error{0};
    bool _frame_text = false;
};

#endif // ESP_PLATFORM
#endif // FN_WEBSOCKET_CLIENT_H
