/* TCP/TLS connection using Mongoose */
#include "mongoose.h"
#undef mkdir

#include "fnTcpsConnection.h"
#include "../../include/debug.h"

#include <cstdlib>
#include <cstring>

#define FNTCP_MAX_WRITE_RETRY (10)

// Built-in self-signed cert/key for server mode (edge case); here, not the header.
static const char s_tls_server[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIFnDCCA4SgAwIBAgIUYgGabifgXaAJIIe7KABF4V6e8PIwDQYJKoZIhvcNAQEL\n"
    "BQAwYzELMAkGA1UEBhMCVVMxCzAJBgNVBAgMAk5KMREwDwYDVQQHDAhSZWQgQmFu\n"
    "azEQMA4GA1UECgwHRnVqaU5ldDEQMA4GA1UECwwHRnVqaU5ldDEQMA4GA1UEAwwH\n"
    "RnVqaU5ldDAeFw0yNjAyMDEwMjU2MDdaFw0yODAyMDEwMjU2MDdaMGkxCzAJBgNV\n"
    "BAYTAlVTMQswCQYDVQQIDAJOSjERMA8GA1UEBwwIUmVkIEJhbmsxEDAOBgNVBAoM\n"
    "B0Z1amlOZXQxEDAOBgNVBAsMB0Z1amlOZXQxFjAUBgNVBAMMDWZ1amluZXQubG9j\n"
    "YWwwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIKAoICAQCvIopkYTk0Upl9fTQ9\n"
    "P0aXnXOsSz6MQOgAsrm1xIh3HayLmWSK9RAfHlxiq1qU4YhobWmNrZGscO+FwK38\n"
    "KEx9ZCoI9GO5qHiAAPXwDXntI5+bvhh93rAK3QVNwRqPuUjBBn4WAHyKej3Oy3Xq\n"
    "zMbm0lUWfXcuPTH2/gxH3MN9SKoGisO8eV/GemfqjehMzDpNW+twMd8ltv/QbO1H\n"
    "9vUENjFuQIfnD7SkVx3jugCQgJcF9xmAN53nTc9emhkoyEUzMbbaBuB+e8oHE/Vj\n"
    "PbyctPD2FtTixcgbHoWfU88imkRBYs4xbEvTBZ4Bf0CVBM+MJKiz10d86icWJ+R6\n"
    "M8WwnXIHvsi4w+sChWZhPOvtI1OtZYv0PVD45gRm8AeVRebQ8I1TgdSqLB78QrUH\n"
    "gm8ESzzeu2OqWsXa029Iroc3sg2eUTpBQb/gDorKdZKP4s5eWrqxVYl3pJdvi/27\n"
    "0GzT3xFAFe+R2/FokbiU4FZYM6zwtCFjYpexG9ih9yl/Pd2wUYNvUzbScBuuz/b/\n"
    "9MahTAkCIA+fVDXdYf9GWv7ZVeX015WkWmnMHqHAFOl9NjUCaaXUFWBS4VrPRcCY\n"
    "cflz+DOCcRZJJA8jR2MoOYl2JfIazucKd72XoHZOrqc59Z49zWnwpgTLgqmVM4E3\n"
    "h3VW8Tv4gcpdD5WpYXnJTFHjPwIDAQABo0IwQDAdBgNVHQ4EFgQUZG1+Y5ZEAMxK\n"
    "px4GwgdkskNDqU8wHwYDVR0jBBgwFoAUcFpmEMRVtZp18mh+9dlxllxIabgwDQYJ\n"
    "KoZIhvcNAQELBQADggIBAH+bAdE1IO50VPVbcTgVYJVa2b2El6tIXQr50GbYNso1\n"
    "coLOPs4gRogA838afZdXPvYsAXz3X0SXS+goGHnxrTiTPFoMTVPGathWuH6YZ2Pm\n"
    "TCXSPP/9aJwSt6lZOyVjhZ/UX6JZyNkROR1FyFq6yvrmDpZjXv3NHNQk5OZDBPFz\n"
    "zYzjr4w42spMeolK9ZBkGxiTtlPJOn+CnvNhkGUneEhgUI4e4qkblQfyjkxE5jDx\n"
    "Oekrs6mCYYufDchi9LrlSfeKXGOkHMeQEbrOZh+jK8qqfTkvXmQiJJYPvt7pnwWw\n"
    "eeVP2y+u4Ldu0LcNWhoZ4eyyz/a28+g6rJbAHwT5M2/DhNqfMQkRkuBZzeAnz1B5\n"
    "REfXX7frArM8wYkM+49zHz+JyH0cUol8kzquwtlz7HmaONKQ7+vZtEh8uPp8iMxS\n"
    "5yObbxfqVxSzBYPT3OgplIgdJvxt0s6S7yXP0b4BsA4WgSWrb4Qhy1Fv6oTVO+Lp\n"
    "b7BTvY0uvjeorup7CsED4NpzhwqOxShPFAg1gC5JiIfm+wNdpwXYGO4TA2eRbsLz\n"
    "J1CxtD0PABUOsp9FeAFoztw12bav3nbhdyTOUjqSWbpiU2N7AkUFmHttOkT7tVoZ\n"
    "8V5axnNFmLdRUdzd8r9Ip5CPqUOA17fD4afURV61E7Jgc4MRP/V/6auST08QSA5I\n"
    "-----END CERTIFICATE-----\n";

static const char s_tls_key[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "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\n"
    "2EzVe86WrWpqjThMnmy/FikmINgF1A==\n"
    "-----END PRIVATE KEY-----\n";

fnTcpsConnection::fnTcpsConnection()
{
    _mgr = new mg_mgr();
    mg_log_set(MG_LL_INFO);
    mg_mgr_init(_mgr);
    Debug_printf("fnTcpsConnection: Mongoose manager initialized\r\n");
}

fnTcpsConnection::~fnTcpsConnection()
{
    stop();
    if (_mgr != nullptr)
    {
        mg_mgr_free(_mgr); // freed once, here (not in stop())
        delete _mgr;
        _mgr = nullptr;
    }
}

int fnTcpsConnection::setTimeout(uint32_t seconds)
{
    _timeout = seconds * 1000; // Store in milliseconds
    return 0;
}

bool fnTcpsConnection::connected()
{
    if (_is_client)
    {
        return _outbound_conn != nullptr && _outbound_conn->is_connecting == 0 &&
               _outbound_conn->is_client == 1 && _awaiting_tls_handshake == false;
    }
    if (_is_server)
    {
        return _inbound_conn != nullptr && _inbound_conn->is_accepted == 1;
    }
    return false;
}

// Returns the number of bytes waiting to be read.
size_t fnTcpsConnection::available()
{
    if (!_is_client && !_is_server)
        return 0;

    if (!_is_polling)
        mg_mgr_poll(_mgr, 100);

    // Re-read after poll: MG_EV_CLOSE may have nulled the pointer.
    struct mg_connection *conn = _is_client ? _outbound_conn : _inbound_conn;
    return conn != nullptr ? conn->recv.len : 0;
}

// read data
int fnTcpsConnection::read(uint8_t *buf, size_t size)
{
    struct mg_connection *reading_conn = _is_client ? _outbound_conn : _inbound_conn;
    if ((!_is_client && !_is_server) || reading_conn == nullptr)
    {
        Debug_printf("fnTcpsConnection: Tried to read without a connection\r\n");
        return -1;
    }

    struct mg_iobuf *io = &reading_conn->recv;
    size_t rlen = io->len < size ? io->len : size;
    if (rlen)
    {
        memcpy(buf, io->buf, rlen);
        mg_iobuf_del(io, 0, rlen); // consume what we handed back
    }
    return (int)rlen;
}

// Read one byte of data. Return read byte or negative value for error
int fnTcpsConnection::read()
{
    uint8_t data = 0;
    int res = read(&data, 1);
    if (res < 0)
        return res;
    if (res == 0)
        return -1;
    return data;
}

// Read bytes of data up until the size of our buffer or when we get our terminator
int fnTcpsConnection::read_until(char terminator, char *buf, size_t size)
{
    if (buf == nullptr || size < 1)
        return 0;

    size_t count = 0;
    while (count < size)
    {
        int c = read();
        if (c < 0 || c == terminator)
            break;

        *buf++ = (char)c;
        count++;
    }
    return count;
}

// Writes data to the active connection. Returns number of bytes written/sent.
size_t fnTcpsConnection::write(const uint8_t *buf, size_t size)
{
    struct mg_connection *writing_conn = _is_client ? _outbound_conn : _inbound_conn;
    if ((!_is_server && !_is_client) || writing_conn == nullptr)
    {
        Debug_printf("fnTcpsConnection: tried to write to a closed connection\r\n");
        return 0;
    }

    _bytes_written = 0;
    _writing_data = true;
    _write_error = false;

    // Add data to the send buffer
    mg_send(writing_conn, buf, size);

    int retry = FNTCP_MAX_WRITE_RETRY;
    while (retry)
    {
        uint64_t start_time = mg_millis();
        // Poll until sent, errored, or timed out (both flags set by the handler).
        _is_polling = true;
        while ((_bytes_written < (long)size) && (mg_millis() - start_time) < (uint64_t)_timeout &&
               !_write_error)
        {
            mg_mgr_poll(_mgr, 100);
        }
        _is_polling = false;

        if (_write_error)
        {
            Debug_printf("fnTcpsConnection: write error, trying again (attempt %d of %d)\r\n",
                         (FNTCP_MAX_WRITE_RETRY - retry), FNTCP_MAX_WRITE_RETRY);
            _write_error = false;
            retry--;
        }
        else if (_bytes_written != (long)size)
        {
            Debug_printf("fnTcpsConnection: Write timed out or sent too few bytes. "
                         "Sent %ld of %zu.\r\n",
                         _bytes_written, size);
            _writing_data = false;
            return (size_t)_bytes_written;
        }
        else
        {
            _writing_data = false;
            return (size_t)_bytes_written;
        }
    }
    _writing_data = false;
    return 0;
}

// Send std::string of data
size_t fnTcpsConnection::write(const std::string str)
{
    return write((const uint8_t *)str.c_str(), str.length());
}

// Send zero-terminated string of data
size_t fnTcpsConnection::write(const char *buff)
{
    if (buff == nullptr)
        return 0;
    return write((const uint8_t *)buff, strlen(buff));
}

// Send just one byte of data
size_t fnTcpsConnection::write(uint8_t data) { return write(&data, 1); }

void fnTcpsConnection::stop()
{
    // Tear down connections only; never free the manager (stop() may be called
    // repeatedly). The manager is freed once in the destructor.
    _cleanup();
    if (_mgr != nullptr)
        mg_mgr_poll(_mgr, 0); // let mongoose process the queued closes
    Debug_printf("fnTcpsConnection: stopped connection\r\n");
}

in_addr_t fnTcpsConnection::remoteIP() const
{
    if (_outbound_conn == nullptr)
        return IPADDR_NONE;
    return (in_addr_t)_outbound_conn->rem.ip4;
}

uint16_t fnTcpsConnection::remotePort() const
{
    if (_outbound_conn == nullptr)
        return 0;
    return _outbound_conn->rem.port;
}

in_addr_t fnTcpsConnection::localIP() const
{
    if (_outbound_conn == nullptr)
        return IPADDR_NONE;
    return (in_addr_t)_outbound_conn->loc.ip4;
}

uint16_t fnTcpsConnection::localPort() const
{
    if (_outbound_conn == nullptr)
        return 0;
    return _outbound_conn->loc.port;
}

// Load the client-mode CA bundle; source differs by platform (SPIFFS vs POSIX).
bool fnTcpsConnection::_load_ca()
{
    if (!_ca.empty())
        return true; // already loaded

    struct mg_str ca = {};
#ifdef ESP_PLATFORM
    ca = mg_file_read(&mg_fs_posix, "/spiffs/ca.pem"); // flashed into SPIFFS
#else
    // System trust store, else the bundle we ship.
#if defined(__APPLE__)
    ca = mg_file_read(&mg_fs_posix, "/etc/ssl/cert.pem");
#elif defined(__linux__)
    ca = mg_file_read(&mg_fs_posix, "/etc/ssl/certs/ca-certificates.crt");
#endif
    if (ca.len == 0)
    {
        if (ca.buf != nullptr)
            free(ca.buf);
        ca = mg_file_read(&mg_fs_posix, "data/ca.pem");
    }
#endif

    if (ca.buf != nullptr && ca.len > 0)
    {
        _ca.assign(ca.buf, ca.len);
        free(ca.buf);
        Debug_printf("fnTcpsConnection: loaded CA bundle (%zu bytes)\r\n", _ca.size());
        return true;
    }
    if (ca.buf != nullptr)
        free(ca.buf);
    Debug_printf("fnTcpsConnection: no CA bundle found; peer verification disabled\r\n");
    return false;
}

// Load the certificate/key presented when acting as a TLS server.
bool fnTcpsConnection::_load_server_cert()
{
    // TODO: let the operator supply cert/key (filesystem / web UI).
    _server_cert = s_tls_server;
    _server_key = s_tls_key;
    return !_server_cert.empty() && !_server_key.empty();
}

// Create a secure connection. Like fnTcpClient, returns 1 on success, 0 on failure.
int fnTcpsConnection::connect(const char *host, uint16_t port, int32_t timeout)
{
    if (_mgr == nullptr)
    {
        Debug_printf("fnTcpsConnection: manager not initialized\r\n");
        return 0;
    }

    // Clean up any existing connection
    _cleanup();

    // Build URL with tcps:// scheme
    char url[256];
    snprintf(url, sizeof(url), "tcps://%s:%u", host, port);

    if (mg_url_is_ssl(url) != 1)
    {
        Debug_printf("fnTcpsConnection: tried to open an insecure connection\r\n");
        return 0;
    }

    _load_ca();    // load CA before the handshake
    _host = host;  // kept for TLS SNI

    _awaiting_tls_handshake = true;
    _outbound_conn = mg_connect(_mgr, url, _event_handler, this);

    if (_outbound_conn == nullptr)
    {
        Debug_printf("fnTcpsConnection: mg_connect failed\r\n");
        _awaiting_tls_handshake = false;
        return 0;
    }

    // setup timeout
    uint64_t start_time = mg_millis();
    uint64_t connection_timeout = (timeout < 0) ? (uint64_t)_timeout : (uint64_t)timeout;

    _is_polling = true;
    // Wait for connect + TLS handshake (handler clears the flag / nulls conn).
    while (_outbound_conn != nullptr &&
           (_outbound_conn->is_connecting || _awaiting_tls_handshake) &&
           (mg_millis() - start_time) < connection_timeout)
    {
        mg_mgr_poll(_mgr, 1000);
    }
    _is_polling = false;

    if (_outbound_conn != nullptr && _awaiting_tls_handshake == false)
    {
        _is_client = true;
        return 1;
    }

    // Connection timed out or failed; reset state and report failure.
    _awaiting_tls_handshake = false;
    _cleanup();
    return 0;
}

// same as above, but takes an IP address. Since Mongoose will handle both IP
// addresses and domain names this isn't strictly necessary, but it is how
// fnTcpClient works.
int fnTcpsConnection::connect(in_addr_t ip, uint16_t port, int32_t timeout)
{
    char ip_string_buffer[INET_ADDRSTRLEN];
    const void *src_ptr = &(ip);
    const char *ip_as_string = inet_ntop(AF_INET, src_ptr, ip_string_buffer, INET_ADDRSTRLEN);
    if (ip_as_string == nullptr)
        return 0;
    return connect(ip_as_string, port, timeout);
}

// Accepts an incoming connection. Returns 0 on success, 1 on error.
int fnTcpsConnection::accept_connection()
{
    Debug_printf("fnTcpsConnection: accepting inbound connection\r\n");

    if (!_is_server || _listener_conn == nullptr || _listener_conn->is_listening == 0)
    {
        Debug_printf(
            "fnTcpsConnection: Tried to accept an inbound connection while not listening\r\n");
        return 1;
    }

    // Setup timeout
    uint64_t start_time = mg_millis();
    uint64_t timeout = (uint64_t)_timeout;

    _is_polling = true;
    // Poll until we have an established inbound TLS connection or we time out.
    while ((_inbound_conn == nullptr || _inbound_conn->is_tls_hs == 1 ||
            _inbound_conn->is_accepted == 0 || _inbound_conn->is_tls == 0) &&
           (mg_millis() - start_time) < timeout)
    {
        mg_mgr_poll(_mgr, 1000);
    }
    _is_polling = false;

    if (_inbound_conn != nullptr && _inbound_conn->is_tls == 1 && _inbound_conn->is_accepted == 1)
        return 0;

    Debug_printf("fnTcpsConnection: timed out waiting for incoming connection or connection "
                 "failed\r\n");
    return 1;
}

// Configures a listening TCP socket on given port
// Returns 0 for error, 1 for success, like fnTcpServer.
int fnTcpsConnection::begin_listening(uint16_t port)
{
    if (_is_server && _listener_conn != nullptr)
    {
        Debug_printf("fnTcpsConnection: TCPS Server already listening. Aborting.\r\n");
        return 0;
    }

    if (_mgr == nullptr)
    {
        Debug_printf("fnTcpsConnection: manager not initialized\r\n");
        return 0;
    }

    // Need a cert before accepting TLS clients.
    if (!_load_server_cert())
    {
        Debug_printf("fnTcpsConnection: no server certificate available\r\n");
        return 0;
    }

    // Clean up any existing connection
    _cleanup();

    // Build URL with tcps:// scheme
    char url[256];
    snprintf(url, sizeof(url), "tcps://0.0.0.0:%u", port);

    if (mg_url_is_ssl(url) != 1)
    {
        Debug_printf("fnTcpsConnection: tried to open an insecure listening connection\r\n");
        return 0;
    }

    // Tell Mongoose to start listening
    _listener_conn = mg_listen(_mgr, url, _event_handler, this);

    if (_listener_conn == nullptr)
    {
        Debug_printf("fnTcpsConnection: could not create a listening connection\r\n");
        return 0;
    }

    // mg_listen sets is_listening synchronously on success.
    if (_listener_conn->is_listening == 0)
    {
        Debug_printf("fnTcpsConnection: listener failed to start\r\n");
        _cleanup();
        return 0;
    }

    Debug_printf("fnTcpsConnection: listening connection created\r\n");
    _is_server = true;
    return 1;
}

// Returns true if a client has connected to the server
bool fnTcpsConnection::hasClient()
{
    if (!_is_server)
    {
        Debug_printf(
            "fnTcpsConnection: `hasClient` called on a client, not server, connection\r\n");
        return false;
    }

    // If polling isn't happening elsewhere, poll once to pick up a new client.
    if (!_is_polling)
        mg_mgr_poll(_mgr, 100);

    return _inbound_conn != nullptr && _inbound_conn->is_accepted;
}

void fnTcpsConnection::_cleanup()
{
    if (_outbound_conn != nullptr)
    {
        _outbound_conn->is_draining = 1;
        _outbound_conn = nullptr;
    }

    if (_inbound_conn != nullptr)
    {
        _inbound_conn->is_closing = 1;
        _inbound_conn = nullptr;
    }

    if (_listener_conn != nullptr)
    {
        _listener_conn->is_closing = 1;
        _listener_conn = nullptr;
    }
    _is_server = false;
    _is_client = false;
}

// Event handler for Mongoose
void fnTcpsConnection::_event_handler(struct mg_connection *c, int ev, void *ev_data)
{
    // the connection contains a pointer to the instance in fn_data
    fnTcpsConnection *client = (fnTcpsConnection *)c->fn_data;

    switch (ev)
    {
    case MG_EV_ERROR: {
        Debug_printf("fnTcpsConnection: Error: %s\r\n", (char *)ev_data);
        if (client->_writing_data)
        {
            // Break the write loop early instead of waiting for the timeout.
            client->_write_error = true;
        }
        break;
    }

    case MG_EV_CONNECT: {
        Debug_printf("fnTcpsConnection: connected, starting TLS handshake with server\r\n");
        struct mg_tls_opts opts = {};
        if (!client->_ca.empty())
            opts.ca = mg_str_n(client->_ca.data(), client->_ca.size());
        else
            Debug_printf("fnTcpsConnection: no CA loaded, server will not be verified\r\n");
        // SNI, needed by most virtual-hosted servers.
        if (!client->_host.empty())
            opts.name = mg_str_n(client->_host.data(), client->_host.size());
        mg_tls_init(c, &opts);
        break;
    }

    case MG_EV_ACCEPT: {
        Debug_printf("fnTcpsConnection: accepted an inbound connection\r\n");
        client->_inbound_conn = c;
        struct mg_tls_opts opts = {};
        opts.cert = mg_str_n(client->_server_cert.data(), client->_server_cert.size());
        opts.key = mg_str_n(client->_server_key.data(), client->_server_key.size());
        mg_tls_init(c, &opts);
        Debug_printf("fnTcpsConnection: starting TLS handshake\r\n");
        client->_awaiting_tls_handshake = true;
        break;
    }

    case MG_EV_TLS_HS: {
        Debug_printf("fnTcpsConnection: TLS handshake completed successfully\r\n");
        client->_awaiting_tls_handshake = false;
        break;
    }

    case MG_EV_READ: {
        Debug_printf("fnTcpsConnection: received %d bytes\r\n", (int)c->recv.len);
        break;
    }

    case MG_EV_WRITE: {
        // ev_data is `long *` bytes for this event; accumulate.
        long *bytes_written = (long *)ev_data;
        client->_bytes_written += *bytes_written;
        break;
    }

    case MG_EV_CLOSE: {
        Debug_printf("fnTcpsConnection: connection closed\r\n");
        // Drop our pointer so we never use it after mongoose frees it.
        if (c == client->_outbound_conn)
            client->_outbound_conn = nullptr;
        if (c == client->_inbound_conn)
            client->_inbound_conn = nullptr;
        if (c == client->_listener_conn)
            client->_listener_conn = nullptr;
        break;
    }
    }
}
