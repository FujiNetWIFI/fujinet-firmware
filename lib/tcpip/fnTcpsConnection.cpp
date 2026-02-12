/* TCP Client using Mongoose for TLS*/
#include "fnTcpsConnection.h"
#include "fnDNS.h"
#include "../../include/debug.h"
#include <cstdlib>
#include <ctype.h>
#include <iostream>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <sstream>
#include <string>
#include <vector>
#include "mongoose.h"
#include <cstring>

#define FNTCP_MAX_WRITE_RETRY (10)
fnTcpsConnection::~fnTcpsConnection()
{
    stop();
    if (_mgr != nullptr)
    {
        mg_mgr_free(_mgr);
        delete _mgr;
        _mgr = nullptr;
    }
}

int fnTcpsConnection::setTimeout(uint32_t seconds)
{
    _timeout = seconds * 1000; // Store in milliseconds
    return 0;
}

uint8_t fnTcpsConnection::connected()
{
    if ((!_is_client && !_is_server) ||
        (_outbound_conn == nullptr && _inbound_conn == nullptr))
    {
        return false;
    }

    if (_is_client)
    {
        if (_outbound_conn == nullptr)
            return false;
        // todo: i don't like this
        else if (_outbound_conn->is_connecting == 0 && _outbound_conn->is_client == 1 &&
                 _awaiting_tls_handshake == false)
        {
            return true;
        }
    }
    else // is server
    {
        if (_inbound_conn == nullptr)
        {
            return false;
        }
        else if (_inbound_conn->is_accepted == 1)
        {
            return true;
        }
        else
        {
            return false;
        }
    }
}

// Returns the number of bytes waiting to be read.
size_t fnTcpsConnection::available()
{
    if (_is_server)
    {
        if (!_is_polling)
            mg_mgr_poll(_mgr, 100);
        return _inbound_conn->recv.len;
    }

    if (_is_client)
    {
        if (!_is_polling)
            mg_mgr_poll(_mgr, 100);
        return _outbound_conn->recv.len;
    }
}

// read data
int fnTcpsConnection::read(uint8_t *buf, size_t size)
{
    if ((!_is_client && !_is_server) or
        (_outbound_conn == nullptr && _inbound_conn == nullptr))
    {
        Debug_printf("Tried to read without a connection\r\n");
        return -1;
    }

    struct mg_connection *reading_conn;
    if (_is_client)
        reading_conn = _outbound_conn;
    else
        reading_conn = _inbound_conn;

    size_t rlen = reading_conn->recv.len;

    rlen = reading_conn->recv.len;
    if (rlen)
    {
        struct mg_iobuf *io = &reading_conn->recv;
        if (io->len > 0)
        {
            // Append data to receive buffer
            size_t old_len = reading_conn->recv.size;
            memcpy(buf, io->buf, rlen);
            // set the buffer length to 0
            io->len = 0;
        }
    }
    return rlen;
}

// Read one byte of data. Return read byte or negative value for error
// TODO: verify this
int fnTcpsConnection::read()
{
    uint8_t data = 0;
    int res = read(&data, 1);
    if (res < 0)
        return res;
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
    Debug_printf("fnTcpConnection: writing data\r\n");
    if ((!_is_server && !_is_client) ||
        (_outbound_conn == nullptr && _inbound_conn == nullptr))
    {
        Debug_printf("fnTcpsConnection: tried to write to a closed connection\r\n");
        return 0;
    }
    mg_connection *writing_conn = nullptr;
    if (_is_client)
    {
        writing_conn = _outbound_conn;
    }
    else
    {
        writing_conn = _inbound_conn;
    }

    _bytes_written = 0;
    _writing_data = true;

    // Add data to the send buffer
    mg_send(writing_conn, buf, size);

    int retry = FNTCP_MAX_WRITE_RETRY;
    while (retry)
    {
        uint64_t start_time = mg_millis();
        // Wait for either the data to be sent, an error, or the timeout to expire.
        // `_bytes_written` is updated by the event handler for `MG_EV_WRITE`
        // `_write_error` is set to true when the event `MG_EV_ERROR` is received and
        // `_writing_data` flag is true
        _is_polling = true;
        while ((_bytes_written < size) && (mg_millis() - start_time) < _timeout &&
               !_write_error)
        {
            mg_mgr_poll(_mgr, 100);
        }
        _is_polling = false;

        if (_write_error)
        {
            Debug_printf("fnTcpsConnection: write error, trying again (attempt %d of %d)",
                         (FNTCP_MAX_WRITE_RETRY - retry), FNTCP_MAX_WRITE_RETRY);
            _write_error = false;
            retry--;
        }
        else if (_bytes_written != size)
        {
            Debug_printf("fnTcpsConnection: Write timed out or sent too few bytes. Sent %d "
                         "bytes of %d.\r\n",
                         _bytes_written, size);
            _writing_data = false;
            return _bytes_written;
        }
        else
        {
            _writing_data = false;
            return _bytes_written;
        }
    }
}

// Send std::string of data
size_t fnTcpsConnection::write(const std::string str)
{
    return write((uint8_t *)str.c_str(), str.length());
}

// Send zero-terminated string of data
size_t fnTcpsConnection::write(const char *buff)
{
    if (buff == nullptr)
        return 0;
    size_t len = strlen(buff);
    return write((uint8_t *)buff, len);
}

// Send just one byte of data
size_t fnTcpsConnection::write(uint8_t data) { return write(&data, 1); }

void fnTcpsConnection::stop()
{
    _cleanup();
    mg_mgr_free(_mgr);
    Debug_printf("fnTcpsConnection: stopped connection\r\n");
}

in_addr_t fnTcpsConnection::remoteIP() const
{
    if (_outbound_conn == nullptr)
    {
        return IPADDR_NONE;
    }
    else
    {
        return (uint32_t)_outbound_conn->rem.ip;
    }
}

uint16_t fnTcpsConnection::remotePort() const
{
    if (_outbound_conn == nullptr)
        return 0;
    else
        return _outbound_conn->rem.port;
}

in_addr_t fnTcpsConnection::localIP() const
{
    if (_outbound_conn == nullptr)
        return IPADDR_NONE;
    else
        return _outbound_conn->loc.ip4;
}

uint16_t fnTcpsConnection::localPort() const
{
    if (_outbound_conn == nullptr)
        return 0;
    else
        return _outbound_conn->loc.port;
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
    snprintf(url, sizeof(url), "tcps://%s:%d", host, port);

    if (mg_url_is_ssl(url) != 1)
    {
        Debug_printf("fnTcpsConnection: tried to open an insecure connection\r\n");
        return 1;
    }

    _awaiting_tls_handshake = true;
    _outbound_conn = mg_connect(_mgr, url, _event_handler, this);

    if (_outbound_conn == nullptr)
    {
        Debug_printf("fnTcpsConnection: mg_connect failed\r\n");
        return 0;
    }

    // setup timeout
    uint64_t start_time = mg_millis();
    uint64_t connection_timeout = (_timeout < 0) ? 30000 : _timeout;

    _is_polling = true;
    // Wait for the connection, including TLS handshake, to complete or for
    // the timeout to expire. `_awaiting_tls_handshake` will be set to false
    // by the event handler when the MG_EV_TLS_HS event is received.
    while ((_outbound_conn->is_connecting || _awaiting_tls_handshake) &&
           (mg_millis() - start_time) < connection_timeout)
    {
        mg_mgr_poll(_mgr, 1000);
    }
    _is_polling = false;

    // `_awaiting_tls_handshake` will be set to false by the event handler when the TLS
    // handshake succeeds. If it's still true, it means we timed out or something else went
    // wrong.
    if (_awaiting_tls_handshake == false)
    {
        // Set the `_is_client` flag to show this is a client, not server, connection.
        _is_client = true;
        return 1;
    }
    else
    {
        // reset the `_awaiting_tls_handshake` flag and return 0
        _awaiting_tls_handshake = false;
        return 0;
    }
}

// same as above, but takes an IP address. Since Mongoose will handle both IP
// addresses and domain names this isn't strictly necessary, but it is how
// fnTcpClient works.
int fnTcpsConnection::connect(in_addr_t ip, uint16_t port, int32_t timeout)
{
    char ip_string_buffer[INET_ADDRSTRLEN];
    const void *src_ptr = &(ip);
    const char *ip_as_string = inet_ntop(AF_INET, src_ptr, ip_string_buffer, INET_ADDRSTRLEN);
    return connect(ip_as_string, port, timeout);
}

// Accepts a incoming connection. Returns 0 on success, 1 on error.
int fnTcpsConnection::accept_connection()
{
    Debug_printf("fnTcpsConnection: accepting inbound connection\r\n");

    if (!_is_server or _listener_conn->is_listening == 0)
    {
        Debug_printf(
            "fnTcpsConnection: Tried to accept an inbound connection while not listening\r\n");
        return 1;
    }

    // `_awaiting_tls_handshake` will be set to false by event handler when
    // MG_EV_TLS_HS event is received.
    _awaiting_tls_handshake == true;

    // Setup timeout
    uint64_t start_time = mg_millis();
    uint64_t timeout = (_timeout < 0) ? 30000 : _timeout; // Default 30s timeout

    _is_polling = true;
    // Call `mg_mgr_poll` until we have an established inbound connection with
    // TLS, or until the timeout expires.
    while ((_inbound_conn == nullptr || _inbound_conn->is_tls_hs == 1 ||
            _inbound_conn->is_accepted == 0 || _inbound_conn->is_tls == 0 ||
            _awaiting_tls_handshake == true) &&
           (mg_millis() - start_time) < timeout)
    {
        mg_mgr_poll(_mgr, 1000);
    }
    _is_polling = false;

    if (_inbound_conn->is_tls == 1 && !_awaiting_tls_handshake)
        return 0;
    else
        Debug_printf("fnTcpsConnection: timed out waiting for incoming connection\r\n");
    return 1;
}

// Configures a listening TCP socket on given port
// Returns 0 for error, 1 for success, like fnTcpServer.
int fnTcpsConnection::begin_listening(uint16_t port)
{
    if (_is_server && _listener_conn != nullptr)
    {
        Debug_printf("fnTcpsServer: TCPS Server already listening. Aborting.\r\n");
        return 0;
    }

    if (_mgr == nullptr)
    {
        Debug_printf("fnTcpsServer: manager not initialized\r\n");
        return 0;
    }

    // Clean up any existing connection
    _cleanup();

    // Build URL with tcps:// scheme
    char url[256];
    snprintf(url, sizeof(url), "tcps://0.0.0.0:%d", port);

    if (mg_url_is_ssl(url) != 1)
    {
        Debug_printf("fnTcpsConnection: tried to open an insecure listening connection\r\n");
        return 1;
    }

    // Tell Mongoose to start listening
    _listener_conn = mg_listen(_mgr, url, _event_handler, this);

    if (_listener_conn == nullptr)
    {
        Debug_printf("fnTcpsConnection: could not create a listening connection\r\n");
        return 0;
    }

    // Setup timeout
    uint64_t start_time = mg_millis();
    uint64_t listener_timeout = (_timeout < 0) ? 30000 : _timeout; // Default 30s timeout

    _is_polling = true;

    // Wait for the new connection's `is_listening` flag to be set to 1,
    // or for the timeout to expire.
    while ((_listener_conn->is_listening == 0) &&
           (mg_millis() - start_time) < listener_timeout)
    {
        mg_mgr_poll(_mgr, 1000);
    }
    _is_polling = false;

    if (_listener_conn->is_listening == 0)
    {
        Debug_printf("fnTcpsConnection: timed out waiting for listener to be setup\r\n");
        _is_server = false;
        return 0;
    }
    else
    {
        Debug_printf("fnTcpsConnection: listening connection created\r\n");
        // Set the `_is_server` flag to indicate this is a server, not client
        _is_server = true;
        return 1;
    }
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

    if (_inbound_conn == nullptr)
    {
        // If `mg_mgr_poll` isn't being run elsewhere, run it once
        // to see if a client is trying to connect.
        if (!_is_polling)
            mg_mgr_poll(_mgr, 100);
        return false;
    }

    if (_is_server and _inbound_conn->is_accepted)
    {
        // Client is connected
        return true;
    }
    else
    {
        // If `mg_mgr_poll` isn't being run elsewhere, run it once
        // to see if a client is trying to connect.
        if (!_is_polling)
            mg_mgr_poll(_mgr, 100);
        return false;
    }
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
    case MG_EV_OPEN: {
        Debug_printf("fnTcpsConnection: opened listening connection\r\n");
        break;
    }

    case MG_EV_ERROR: {
        Debug_printf("fnTcpsConnection: Error: %s\r\n", (char *)ev_data);
        if (client->_writing_data)
        {
            // this error occurred while trying to write data; this is used
            // to break out of the write loop without waiting for the timeout
            // to expire.
            client->_write_error = true;
        }
        break;
    }

    case MG_EV_POLL: {
        break;
    }
    case MG_EV_RESOLVE: {
        Debug_printf("fnTcpsConnection: name resolution succeeded\r\n");
        break;
    }

    case MG_EV_CONNECT: {
        Debug_printf("fnTcpsConnection: Connected\r\n");
        Debug_printf("fnTcpsConnection: Starting TLS handshake with server\r\n");
        struct mg_tls_opts opts = {.ca = mg_str(client->c_tls_ca)};
        mg_tls_init(c, &opts);
        break;
    }

    case MG_EV_ACCEPT: {
        Debug_printf("fnTcpsConnection: accepted an inbound connection\r\n");
        client->_inbound_conn = &*c;
        mg_tls_init(c, &client->tls_opts);
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
        int *bytes_written = (int *)ev_data;
        client->_bytes_written = *bytes_written;

        break;
    }

    case MG_EV_CLOSE: {
        Debug_printf("fnTcpsConnection: connection closed\r\n");
        break;
    }
    }
}