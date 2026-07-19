/**
 * fnTcpClientSecure - blocking TLS client socket (esp-tls on ESP, mongoose on PC).
 */

#include "fnTcpClientSecure.h"

#include <cstring>

#include "../../include/debug.h"
#include "fnSystem.h"

#ifdef ESP_PLATFORM
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "lwip/sockets.h"
#else
#include "mongoose.h"
#include <cstdlib>
#include <sstream>
#endif

// Backend read timeout per fill() call; the outer readLine/readN loops enforce
// the caller's overall deadline.
#define FNTLS_FILL_MS 2000

fnTcpClientSecure::fnTcpClientSecure() {}

fnTcpClientSecure::~fnTcpClientSecure() { stop(); }

// ─── ESP backend (esp-tls) ────────────────────────────────────────────────────
#ifdef ESP_PLATFORM

bool fnTcpClientSecure::connect(const std::string &host, uint16_t port, uint32_t timeout_ms)
{
    _host = host;
    _rx.clear();
    _error = false;
    _connected = false;

    esp_tls_cfg_t cfg = {};
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = (int)timeout_ms;

    esp_tls_t *tls = esp_tls_init();
    if (!tls)
        return false;

    int r = esp_tls_conn_new_sync(host.c_str(), (int)host.size(), (int)port, &cfg, tls);
    if (r != 1)
    {
        Debug_printf("fnTcpClientSecure: TLS connect to %s:%u failed (%d)\r\n", host.c_str(), port, r);
        esp_tls_conn_destroy(tls);
        return false;
    }
    _tls = tls;

    int sockfd = -1;
    if (esp_tls_get_conn_sockfd(tls, &sockfd) == ESP_OK && sockfd >= 0)
    {
        struct timeval tv;
        tv.tv_sec = FNTLS_FILL_MS / 1000;
        tv.tv_usec = (FNTLS_FILL_MS % 1000) * 1000;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        _sockfd = sockfd;
    }

    _connected = true;
    return true;
}

int fnTcpClientSecure::fill(uint32_t timeout_ms)
{
    (void)timeout_ms; // governed by SO_RCVTIMEO
    if (!_tls)
        return -1;
    uint8_t buf[512];
    int r = esp_tls_conn_read((esp_tls_t *)_tls, buf, sizeof(buf));
    if (r > 0)
    {
        _rx.append((char *)buf, r);
        return r;
    }
    if (r == ESP_TLS_ERR_SSL_WANT_READ || r == ESP_TLS_ERR_SSL_WANT_WRITE)
        return 0; // socket timeout / retry
    return -1;    // r == 0 (closed) or error
}

size_t fnTcpClientSecure::write(const std::string &s)
{
    if (!_tls)
        return 0;
    size_t sent = 0;
    while (sent < s.size())
    {
        int w = esp_tls_conn_write((esp_tls_t *)_tls, s.data() + sent, s.size() - sent);
        if (w > 0)
            sent += w;
        else if (w == ESP_TLS_ERR_SSL_WANT_READ || w == ESP_TLS_ERR_SSL_WANT_WRITE)
            continue;
        else
        {
            _error = true;
            break;
        }
    }
    return sent;
}

void fnTcpClientSecure::stop()
{
    if (_tls)
    {
        esp_tls_conn_destroy((esp_tls_t *)_tls);
        _tls = nullptr;
    }
    _connected = false;
}

// ─── PC backend (mongoose) ────────────────────────────────────────────────────
#else

// Load the system CA bundle, keeping only CERTIFICATE blocks (mongoose's mbedTLS
// loader treats the buffer as PEM only when it starts with '-'). Mirrors
// mgWebSocketClient::load_ca / mgHttpClient.
void fnTcpClientSecure::load_ca()
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

    _certStore.clear();
    if (tempCa.buf != nullptr)
    {
        std::istringstream certStream(std::string(tempCa.buf, tempCa.len));
        std::string line;
        bool inCertBlock = false;
        while (std::getline(certStream, line))
        {
            if (line.find("-----BEGIN CERTIFICATE-----") != std::string::npos)
                inCertBlock = true;
            if (inCertBlock)
                _certStore += line + "\n";
            if (line.find("-----END CERTIFICATE-----") != std::string::npos)
                inCertBlock = false;
        }
        free((void *)tempCa.buf);
    }
}

void fnTcpClientSecure::ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
    fnTcpClientSecure *self = (fnTcpClientSecure *)c->fn_data;

    switch (ev)
    {
    case MG_EV_CONNECT:
    {
        // mg_connect does not init TLS itself; do it here (SNI + CA verification).
        struct mg_tls_opts opts = {};
#ifdef SKIP_SERVER_CERT_VERIFY
        opts.skip_verification = 1;
#else
        opts.ca = mg_str_n(self->_certStore.data(), self->_certStore.size());
#endif
        opts.name = mg_str(self->_host.c_str());
        mg_tls_init(c, &opts);
        self->_connected = true;
        break;
    }
    case MG_EV_READ:
        self->_rx.append((const char *)c->recv.buf, c->recv.len);
        mg_iobuf_del(&c->recv, 0, c->recv.len);
        break;
    case MG_EV_ERROR:
        self->_error = true;
        self->_connected = false;
        self->_conn = nullptr;
        break;
    case MG_EV_CLOSE:
        self->_connected = false;
        self->_conn = nullptr;
        break;
    default:
        break;
    }
}

bool fnTcpClientSecure::connect(const std::string &host, uint16_t port, uint32_t timeout_ms)
{
    _host = host;
    _rx.clear();
    _error = false;
    _connected = false;

    _mgr = new mg_mgr();
    mg_mgr_init(_mgr);
    load_ca();

    std::string url = "tcp://" + host + ":" + std::to_string(port);
    _conn = mg_connect(_mgr, url.c_str(), ev_handler, this);
    if (_conn == nullptr)
    {
        _error = true;
        return false;
    }

    uint64_t start = fnSystem.millis();
    while (!_connected && !_error)
    {
        mg_mgr_poll(_mgr, 50);
        if (fnSystem.millis() - start > timeout_ms)
        {
            _error = true;
            break;
        }
    }
    return _connected && !_error;
}

int fnTcpClientSecure::fill(uint32_t timeout_ms)
{
    if (_mgr == nullptr)
        return -1;
    size_t before = _rx.size();
    uint64_t start = fnSystem.millis();
    do
    {
        mg_mgr_poll(_mgr, 50);
        if (_rx.size() > before)
            return (int)(_rx.size() - before);
        if (_error || _conn == nullptr)
            return -1;
    } while (fnSystem.millis() - start <= timeout_ms);
    return 0;
}

size_t fnTcpClientSecure::write(const std::string &s)
{
    if (_conn == nullptr)
        return 0;
    mg_send(_conn, s.data(), s.size());
    mg_mgr_poll(_mgr, 0);
    return s.size();
}

void fnTcpClientSecure::stop()
{
    if (_conn != nullptr)
    {
        _conn->is_draining = 1;
        mg_mgr_poll(_mgr, 0);
        _conn = nullptr;
    }
    _connected = false;
    if (_mgr != nullptr)
    {
        mg_mgr_free(_mgr);
        delete _mgr;
        _mgr = nullptr;
    }
}

#endif // ESP_PLATFORM

// ─── shared line / count reads ────────────────────────────────────────────────

bool fnTcpClientSecure::readLine(std::string &out, uint32_t timeout_ms)
{
    uint64_t start = fnSystem.millis();
    for (;;)
    {
        size_t nl = _rx.find('\n');
        if (nl != std::string::npos)
        {
            out = _rx.substr(0, nl);
            if (!out.empty() && out.back() == '\r')
                out.pop_back();
            _rx.erase(0, nl + 1);
            return true;
        }
        uint64_t elapsed = fnSystem.millis() - start;
        if (elapsed >= timeout_ms)
            return false;
        int r = fill((uint32_t)(timeout_ms - elapsed) < FNTLS_FILL_MS
                         ? (uint32_t)(timeout_ms - elapsed)
                         : FNTLS_FILL_MS);
        if (r < 0)
        {
            // connection closed: hand back any trailing partial line
            if (!_rx.empty())
            {
                out.swap(_rx);
                _rx.clear();
                if (!out.empty() && out.back() == '\r')
                    out.pop_back();
                return true;
            }
            return false;
        }
    }
}

bool fnTcpClientSecure::readN(std::string &out, size_t n, uint32_t timeout_ms)
{
    uint64_t start = fnSystem.millis();
    while (_rx.size() < n)
    {
        uint64_t elapsed = fnSystem.millis() - start;
        if (elapsed >= timeout_ms)
            return false;
        int r = fill((uint32_t)(timeout_ms - elapsed) < FNTLS_FILL_MS
                         ? (uint32_t)(timeout_ms - elapsed)
                         : FNTLS_FILL_MS);
        if (r < 0)
            return false;
    }
    out.assign(_rx, 0, n);
    _rx.erase(0, n);
    return true;
}
