/* TCP/TLS connection using Mongoose */
#ifndef _FN_TCPSCONNECTION_H_
#define _FN_TCPSCONNECTION_H_

#include <cstdint>
#include <string>

#include "compat_inet.h"

// Forward-declared to keep mongoose.h (+ mbedtls) out of this widely-included header.
struct mg_mgr;
struct mg_connection;

class fnTcpsConnection
{
public:
    fnTcpsConnection();
    ~fnTcpsConnection();

    // Public methods for managing TCP with TLS connections
    // These are used both when acting as a server and client
    int setTimeout(uint32_t seconds);
    bool connected();
    size_t available();
    int read();
    int read(uint8_t *buf, size_t size);
    int read_until(char terminator, char *buf, size_t size);
    size_t write(uint8_t data);
    size_t write(const uint8_t *buf, size_t size);
    size_t write(const char *buff);
    size_t write(const std::string str);
    void stop();

    in_addr_t remoteIP() const;
    uint16_t remotePort() const;
    in_addr_t localIP() const;
    uint16_t localPort() const;

    // These methods are used when acting as a client, i.e.,
    // connecting to a server.
    int connect(const char *host, uint16_t port, int32_t timeout = -1);
    int connect(in_addr_t addr, uint16_t port, int32_t timeout = -1);

    // These methods are used when acting as a server, i.e.,
    // when accepting connections from clients
    int accept_connection();
    int begin_listening(uint16_t port = 0);
    bool hasClient(); // returns true if a client connected

private:
    struct mg_mgr *_mgr = nullptr;              // owned; freed once in the destructor
    struct mg_connection *_outbound_conn = nullptr; // client connection
    struct mg_connection *_inbound_conn = nullptr;  // accepted server connection
    struct mg_connection *_listener_conn = nullptr; // listening connection

    bool _is_client = false;
    bool _is_server = false;
    bool _awaiting_tls_handshake = false;
    bool _is_polling = false; // true while mg_mgr_poll is driven elsewhere
    int _timeout = 30000;     // ms
    long _bytes_written = 0;
    bool _writing_data = false;
    bool _write_error = false;

    // Kept alive so the mg_str views handed to mongoose stay valid.
    std::string _host;        // remote host, for TLS SNI in client mode
    std::string _ca;          // CA bundle, client mode
    std::string _server_cert; // certificate, server mode
    std::string _server_key;  // private key, server mode

    void _cleanup();
    bool _load_ca();          // false if no CA found (peer left unverified)
    bool _load_server_cert(); // false if absent

    static void _event_handler(struct mg_connection *c, int ev, void *ev_data);
};

#endif // _FN_TCPSCONNECTION_H_
