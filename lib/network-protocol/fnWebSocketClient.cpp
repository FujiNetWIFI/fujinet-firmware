// ESP32 WebSocket client, backed by esp_websocket_client. Empty on PC builds.
#ifdef ESP_PLATFORM

#include "fnWebSocketClient.h"

#include <cstring>

#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// short read timeout (ms) for an explicit read() with no buffered data
#define WS_READ_TIMEOUT 2000
// poll granularity (ms) while waiting on the esp client's task
#define WS_POLL_STEP 20

fnWebSocketClient::fnWebSocketClient() {}

fnWebSocketClient::~fnWebSocketClient()
{
    stop();
}

void fnWebSocketClient::_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    fnWebSocketClient *self = (fnWebSocketClient *)arg;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (id)
    {
    case WEBSOCKET_EVENT_CONNECTED:
        self->_connected = true;
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        self->_connected = false;
        break;
    case WEBSOCKET_EVENT_DATA:
        // skip control frames: 0x08 close, 0x09 ping, 0x0A pong
        if (data->op_code == 0x08)
        {
            self->_connected = false;
            break;
        }
        if (data->op_code == 0x09 || data->op_code == 0x0A)
            break;
        if (data->data_len > 0 && data->data_ptr != nullptr)
        {
            std::lock_guard<std::mutex> lock(self->_rx_mtx);
            self->_rx.append(data->data_ptr, data->data_len);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        self->_error = 1;
        break;
    default:
        break;
    }
}

bool fnWebSocketClient::connect(const std::string &url, int timeout_ms)
{
    _url = url;
    {
        std::lock_guard<std::mutex> lock(_rx_mtx);
        _rx.clear();
    }
    _connected = false;
    _error = 0;

    esp_websocket_client_config_t cfg = {};
    cfg.uri = _url.c_str();
    if (strncmp(_url.c_str(), "wss:", 4) == 0)
    {
#ifdef SKIP_SERVER_CERT_VERIFY
        cfg.skip_cert_common_name_check = true;
#else
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
#endif
    }

    _client = esp_websocket_client_init(&cfg);
    if (_client == nullptr)
    {
        _error = 1;
        return false;
    }
    esp_websocket_register_events(_client, WEBSOCKET_EVENT_ANY, _event_handler, this);
    if (esp_websocket_client_start(_client) != ESP_OK)
    {
        _error = 1;
        return false;
    }

    int waited = 0;
    while (!_connected && !_error && waited < timeout_ms)
    {
        vTaskDelay(pdMS_TO_TICKS(WS_POLL_STEP));
        waited += WS_POLL_STEP;
    }

    return _connected && !_error;
}

bool fnWebSocketClient::connected()
{
    return _client != nullptr && _connected && esp_websocket_client_is_connected(_client);
}

size_t fnWebSocketClient::available()
{
    std::lock_guard<std::mutex> lock(_rx_mtx);
    return _rx.size();
}

int fnWebSocketClient::read(uint8_t *buf, size_t size)
{
    // bounded wait for data on an explicit read with an empty buffer
    int waited = 0;
    while (true)
    {
        {
            std::lock_guard<std::mutex> lock(_rx_mtx);
            if (!_rx.empty())
                break;
        }
        if (!connected() || waited >= WS_READ_TIMEOUT)
            break;
        vTaskDelay(pdMS_TO_TICKS(WS_POLL_STEP));
        waited += WS_POLL_STEP;
    }

    std::lock_guard<std::mutex> lock(_rx_mtx);
    size_t n = _rx.size() < size ? _rx.size() : size;
    if (n > 0)
    {
        memcpy(buf, _rx.data(), n);
        _rx.erase(0, n);
    }
    return (int)n;
}

int fnWebSocketClient::write(const uint8_t *buf, size_t size)
{
    if (!connected())
        return -1;

    if (_frame_text)
        return esp_websocket_client_send_text(_client, (const char *)buf, size, portMAX_DELAY);
    return esp_websocket_client_send_bin(_client, (const char *)buf, size, portMAX_DELAY);
}

void fnWebSocketClient::stop()
{
    if (_client != nullptr)
    {
        esp_websocket_client_close(_client, portMAX_DELAY);
        esp_websocket_client_stop(_client);
        esp_websocket_client_destroy(_client);
        _client = nullptr;
    }
    _connected = false;
}

#endif // ESP_PLATFORM
