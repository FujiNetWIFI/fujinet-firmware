// PC/desktop WebSocket client, backed by mongoose. Empty on ESP builds.
#ifndef ESP_PLATFORM

#include "mgWebSocketClient.h"

#include <cstring>
#include <cstdlib>

#include "fnSystem.h"

// short read timeout (ms) for an explicit read() with no buffered data
#define WS_READ_TIMEOUT 2000

mgWebSocketClient::mgWebSocketClient() {}

mgWebSocketClient::~mgWebSocketClient()
{
    stop();
}

// Load the system CA bundle into _certStore for wss:// verification.
void mgWebSocketClient::load_ca()
{
#if defined(_WIN32)
    mg_str tempCa = mg_file_read(&mg_fs_posix, "data/ca.pem");
#elif defined(__linux__)
    mg_str tempCa = mg_file_read(&mg_fs_posix, "/etc/ssl/certs/ca-certificates.crt");
#else
    mg_str tempCa = mg_file_read(&mg_fs_posix, "/etc/ssl/cert.pem");
#endif
    if (tempCa.len == 0)
    {
        if (tempCa.buf != nullptr)
            free((void *)tempCa.buf);
        tempCa = mg_file_read(&mg_fs_posix, "data/ca.pem");
    }
    if (tempCa.buf != nullptr)
    {
        _certStore.assign(tempCa.buf, tempCa.len);
        free((void *)tempCa.buf);
    }
    _ca.buf = _certStore.data();
    _ca.len = _certStore.length();
}

void mgWebSocketClient::ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
    mgWebSocketClient *self = (mgWebSocketClient *)c->fn_data;

    switch (ev)
    {
    case MG_EV_CONNECT:
        // mg_ws_connect does not init TLS itself; do it here for wss://
        if (mg_url_is_ssl(self->_url.c_str()))
        {
            struct mg_tls_opts opts = {};
#ifdef SKIP_SERVER_CERT_VERIFY
            opts.ca.buf = nullptr;
#else
            opts.ca = self->_ca;
#endif
            opts.name = mg_url_host(self->_url.c_str());
            mg_tls_init(c, &opts);
        }
        break;
    case MG_EV_WS_OPEN:
        self->_connected = true;
        self->_handshake_done = true;
        break;
    case MG_EV_WS_MSG:
    {
        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        self->_rx.append(wm->data.buf, wm->data.len);
        break;
    }
    case MG_EV_ERROR:
        self->_error = 1;
        self->_connected = false;
        self->_conn = nullptr;
        break;
    case MG_EV_CLOSE:
        self->_closed = true;
        self->_connected = false;
        self->_conn = nullptr;
        break;
    default:
        break;
    }
}

bool mgWebSocketClient::connect(const std::string &url, int timeout_ms)
{
    _url = url;
    _rx.clear();
    _connected = false;
    _handshake_done = false;
    _closed = false;
    _error = 0;

    _mgr = new mg_mgr();
    mg_mgr_init(_mgr);
    load_ca();

    _conn = mg_ws_connect(_mgr, _url.c_str(), ev_handler, this, NULL);
    if (_conn == nullptr)
    {
        _error = 1;
        return false;
    }

    uint64_t start = fnSystem.millis();
    while (!_handshake_done && !_error && !_closed)
    {
        mg_mgr_poll(_mgr, 50);
        if (fnSystem.millis() - start > (uint64_t)timeout_ms)
        {
            _error = 1;
            break;
        }
    }

    return _handshake_done && !_error;
}

bool mgWebSocketClient::connected()
{
    return _connected && _conn != nullptr;
}

size_t mgWebSocketClient::available()
{
    // pull any already-arrived frames without blocking
    if (_mgr != nullptr && _rx.empty() && _connected)
        mg_mgr_poll(_mgr, 0);
    return _rx.size();
}

int mgWebSocketClient::read(uint8_t *buf, size_t size)
{
    if (_mgr == nullptr)
        return -1;

    // bounded wait for data on an explicit read with an empty buffer
    if (_rx.empty() && _connected)
    {
        uint64_t start = fnSystem.millis();
        while (_rx.empty() && _connected && !_error && !_closed)
        {
            mg_mgr_poll(_mgr, 50);
            if (fnSystem.millis() - start > WS_READ_TIMEOUT)
                break;
        }
    }

    size_t n = _rx.size() < size ? _rx.size() : size;
    if (n > 0)
    {
        memcpy(buf, _rx.data(), n);
        _rx.erase(0, n);
    }
    return (int)n;
}

int mgWebSocketClient::write(const uint8_t *buf, size_t size)
{
    if (_conn == nullptr || !_connected)
        return -1;

    // mg_ws_send returns bytes queued incl. frame header; treat >= size as ok
    size_t sent = mg_ws_send(_conn, buf, size, _frame_op);
    mg_mgr_poll(_mgr, 0); // flush
    return sent >= size ? (int)size : (int)sent;
}

void mgWebSocketClient::stop()
{
    if (_conn != nullptr && _connected)
    {
        mg_ws_send(_conn, "", 0, WEBSOCKET_OP_CLOSE);
        mg_mgr_poll(_mgr, 0);
    }
    _conn = nullptr;
    _connected = false;
    if (_mgr != nullptr)
    {
        mg_mgr_free(_mgr);
        delete _mgr;
        _mgr = nullptr;
    }
}

#endif // !ESP_PLATFORM
