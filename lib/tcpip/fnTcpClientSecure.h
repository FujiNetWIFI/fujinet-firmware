#ifndef FNTCPCLIENTSECURE_H
#define FNTCPCLIENTSECURE_H

#include <cstdint>
#include <string>

#ifndef ESP_PLATFORM
struct mg_mgr;
struct mg_connection;
#endif

/**
 * Minimal blocking TLS client socket with line- and exact-count reads, for text
 * protocols such as IMAPS. ESP builds use esp-tls (with the mbedTLS certificate
 * bundle); PC builds use mongoose (mg_tls), the same stack as mgHttpClient.
 */
class fnTcpClientSecure
{
public:
    fnTcpClientSecure();
    ~fnTcpClientSecure();
    fnTcpClientSecure(const fnTcpClientSecure &) = delete;
    fnTcpClientSecure &operator=(const fnTcpClientSecure &) = delete;

    // Connect + TLS handshake to host:port. Returns true on success.
    bool connect(const std::string &host, uint16_t port, uint32_t timeout_ms = 15000);
    bool connected() const { return _connected; }

    // Read one CRLF-terminated line; the terminator is consumed and CR/LF stripped
    // from out. Returns false on timeout or close-with-no-data.
    bool readLine(std::string &out, uint32_t timeout_ms = 15000);

    // Read exactly n bytes. Returns false if the connection closes or times out first.
    bool readN(std::string &out, size_t n, uint32_t timeout_ms = 30000);

    size_t write(const std::string &s);
    void stop();

private:
    // Append one backend read to _rx: returns >0 bytes read, 0 on timeout, -1 on close/error.
    int fill(uint32_t timeout_ms);

    std::string _rx;   // buffered received (decrypted) bytes
    std::string _host; // remote host, kept for SNI
    bool _connected = false;
    bool _error = false;

#ifdef ESP_PLATFORM
    void *_tls = nullptr; // esp_tls_t*
    int _sockfd = -1;
#else
    struct mg_mgr *_mgr = nullptr;
    struct mg_connection *_conn = nullptr;
    std::string _certStore; // owns the CA PEM bytes referenced during the handshake
    void load_ca();
    static void ev_handler(struct mg_connection *c, int ev, void *ev_data);
#endif
};

#endif // FNTCPCLIENTSECURE_H
