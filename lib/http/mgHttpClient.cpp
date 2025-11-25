#ifndef ESP_PLATFORM

#include <cstdlib>
#include <ctype.h>
#include <iostream>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <sstream>
#include <string>
#include <vector>

// #include <mbedtls/debug.h>

#include "mongoose.h"
#undef mkdir

#if defined(_WIN32)

#if MG_TLS == MG_TLS_OPENSSL
// only convert to PEM from windows raw, so only need it here
#include <openssl/pem.h>
#include <openssl/x509.h>
#endif


#if MG_TLS == MG_TLS_MBED
#include <mbedtls/pem.h>
#include <mbedtls/x509_crt.h>
#endif

#include <winsock2.h>
#include <windows.h>
#include <wincrypt.h>
#endif

#include "fnSystem.h"
#include "utils.h"
#include "mgHttpClient.h"

#include "../../include/debug.h"



const char *webdav_depths[] = {"0", "1", "infinity"};

mgHttpClient::mgHttpClient()
{
    // Used for cert debugging:
    // mbedtls_debug_set_threshold(5);

    _buffer_str.clear();
    load_system_certs();
}

// Close connection, destroy any resoruces
mgHttpClient::~mgHttpClient()
{
    close();
}

void mgHttpClient::load_system_certs() {
#if defined(__linux__) || defined(__APPLE__)
    load_system_certs_unix();
#elif defined(_WIN32)
    load_system_certs_windows();
#else
    // report unsupported...
#endif
}

#if defined(_WIN32)

#if MG_TLS == MG_TLS_OPENSSL

// NOTE: This is untested as I have only used mbed tls
std::vector<std::vector<char>> ConvertCertificatesToPEM(const std::vector<std::vector<unsigned char>>& certificates) {
    std::vector<std::vector<char>> pemCertificates;

    for (const auto& certData : certificates) {
        const unsigned char* pCertData = certData.data();
        X509* cert = d2i_X509(NULL, &pCertData, static_cast<long>(certData.size()));
        if (cert) {
            BIO* bio = BIO_new(BIO_s_mem());
            if (PEM_write_bio_X509(bio, cert)) {
                BUF_MEM* bptr;
                BIO_get_mem_ptr(bio, &bptr);
                std::vector<char> pemData(bptr->data, bptr->data + bptr->length);
                pemCertificates.push_back(pemData);
            }
            BIO_free(bio);
            X509_free(cert);
        }
    }

    return pemCertificates;
}
#elif MG_TLS == MG_TLS_MBED


std::vector<std::vector<char>> ConvertCertificatesToPEM(const std::vector<std::vector<unsigned char>>& certificates) {
    const size_t MBEDTLS_PEM_BUFFER_SIZE = 10240;
    std::vector<std::vector<char>> pemCertificates;

    for (const auto& derCert : certificates) {
        mbedtls_x509_crt crt;
        mbedtls_x509_crt_init(&crt);

        // Parse DER certificate
        int ret = mbedtls_x509_crt_parse_der(&crt, derCert.data(), derCert.size());
        if (ret != 0) {
            // we'll just skip this certificate.
            mbedtls_x509_crt_free(&crt);
            continue;
        }

        std::vector<char> pemCert(MBEDTLS_PEM_BUFFER_SIZE, '\0');
        size_t pem_len = 0;

        // Convert DER to PEM
        ret = mbedtls_pem_write_buffer("-----BEGIN CERTIFICATE-----\n",
                                       "-----END CERTIFICATE-----\n",
                                       crt.raw.p, crt.raw.len,
                                       reinterpret_cast<unsigned char*>(pemCert.data()), pemCert.size(), &pem_len);
        if (ret == 0) {
            // Resize to actual PEM length
            pemCert.resize(pem_len - 1); // Exclude the null terminator added by mbedtls_pem_write_buffer
            pemCertificates.push_back(std::move(pemCert));
        }

        mbedtls_x509_crt_free(&crt);
    }

    return pemCertificates;
}

#else
// TODO: if any other MG_TLS type is used, will need to implement conversion to PEM for it.
std::vector<std::vector<char>> ConvertCertificatesToPEM(const std::vector<std::vector<unsigned char>>& certificates) {
    return std::vector<std::vector<char>>();
}

#endif


std::vector<std::vector<unsigned char>> EnumerateCertificates() {
    std::vector<std::vector<unsigned char>> certificates;

    HCERTSTORE hStore = CertOpenSystemStore(NULL, "ROOT");
    if (!hStore) {
        std::cerr << "Failed to open certificate store\n";
        return certificates; // Empty vector on failure
    }

    PCCERT_CONTEXT pCertContext = NULL;
    while ((pCertContext = CertEnumCertificatesInStore(hStore, pCertContext)) != NULL) {
        std::vector<unsigned char> certData(pCertContext->pbCertEncoded, pCertContext->pbCertEncoded + pCertContext->cbCertEncoded);
        certificates.push_back(certData);
    }

    CertCloseStore(hStore, 0);
    return certificates;
}

void mgHttpClient::load_system_certs_windows() {
    auto certificates = EnumerateCertificates();
    auto pemCertificates = ConvertCertificatesToPEM(certificates);
    if (!pemCertificates.empty()) {
        Debug_printf("System certificates loaded, count: %d\n", pemCertificates.size());
        for (const auto& pemData : pemCertificates) {
            concatenatedPEM.append(pemData.begin(), pemData.end());
        }

        ca.ptr = concatenatedPEM.c_str();
        ca.len = concatenatedPEM.length();
    }
    else {
        Debug_printf("WARNING: could not find system certificate file, falling back to local file.\n");
        ca = mg_file_read(&mg_fs_posix, "data/ca.pem");
    }
}

#else // !_WIN32

void mgHttpClient::load_system_certs_unix() {
    int cert_count = 0;
    certDataStorage.clear();

    // Load the initial certificate file
#if defined(__linux__)
    mg_str tempCa = mg_file_read(&mg_fs_posix, "/etc/ssl/certs/ca-certificates.crt");
#else // MAC
    mg_str tempCa = mg_file_read(&mg_fs_posix, "/etc/ssl/cert.pem");
#endif

    if (tempCa.len == 0) {
        Debug_printf("WARNING: could not find system certificate file, falling back to local file.\n");
        // If the initial load fails, try the fallback file
        if (tempCa.buf != NULL) {
            free((void*)tempCa.buf); // Free the memory if it was allocated
        }
        tempCa = mg_file_read(&mg_fs_posix, "data/ca.pem");
    }

    if (tempCa.buf != NULL) {
        // Process the certificate data
        std::string certData(tempCa.buf, tempCa.len);
        std::istringstream certStream(certData);
        std::string line;
        bool inCertBlock = false;

        while (std::getline(certStream, line)) {
            if (line.find("-----BEGIN CERTIFICATE-----") != std::string::npos) {
                inCertBlock = true;
                cert_count++;
            }
            if (inCertBlock) {
                certDataStorage += line + "\n";
            }
            if (line.find("-----END CERTIFICATE-----") != std::string::npos) {
                inCertBlock = false;
            }
        }

        // Free the memory allocated by mg_file_read
        free((void*)tempCa.buf);
    }

    // Update 'ca' to point to the processed certificates stored in 'certDataStorage'
    ca.buf = certDataStorage.data();
    ca.len = certDataStorage.length();

    Debug_printf("System certificates loaded, count: %d\n", cert_count);
}

#endif

// Start an HTTP client session to the given URL
bool mgHttpClient::begin(std::string url)
{
#ifdef VERBOSE_HTTP
    Debug_printf("mgHttpClient::begin \"%s\"\n", url.c_str());
#endif

    _max_redirects = 10;
    _transaction_done = true;

    _post_data = nullptr;
    _post_datalen = 0;

    _handle.reset(new mg_mgr());
    if (_handle == nullptr)
        return false;

    _url = url;
    // For mongoose, lowercase the first 5 characters of the URL, assuming it starts with http:// or https://
    for (size_t i = 0; i < 5 && i < _url.size(); ++i)
        _url[i] = std::tolower(_url[i]);
    mg_mgr_init(_handle.get());
    return true;
}

int mgHttpClient::available()
{
    if (_handle != nullptr && !_transaction_done && _buffer_str.size() == 0)
    {
        _perform_fetch();
    }
    return _buffer_str.size();
}

/*
 Reads HTTP response data
 Return value is bytes stored in buffer or -1 on error
 Buffer will NOT be zero-terminated
 Return value >= 0 but less than dest_bufflen indicates end of data
*/
int mgHttpClient::read(uint8_t *dest_buffer, int dest_bufflen)
{
    if (_handle == nullptr || dest_buffer == nullptr)
        return -1;

    int bytes_copied = 0;
    int bytes_available = _buffer_str.size();

    while (bytes_copied < dest_bufflen)
    {
        if (bytes_available > 0)
        {
            // Copy our buffer to the destination buffer
            int dest_size = dest_bufflen - bytes_copied;
            int bytes_to_copy = dest_size > bytes_available ? bytes_available : dest_size;
#ifdef VERBOSE_HTTP
            Debug_printf("::read from buffer %d\r\n", bytes_to_copy);
#endif
            memcpy(dest_buffer + bytes_copied, _buffer_str.data(), bytes_to_copy);
            _buffer_str.erase(0, bytes_to_copy);
            bytes_copied += bytes_to_copy;
        }
        else
        {
            // If we have no data, try to get some
            if (!_transaction_done)
            {
                _perform_fetch();
                bytes_available = _buffer_str.size();
            }
            if (_status_code >= 400)
            {
                // HTTP client error occurred
                return -1;
            }
            if (bytes_available == 0)
            {
                // No more data to read
#ifdef VERBOSE_HTTP
                Debug_println("::read download done");
#endif
                return bytes_copied;
            }
        }
    }

    return bytes_copied;
}

// Close connection, but keep request resources
void mgHttpClient::close()
{
    Debug_println("mgHttpClient::close");
    _stored_headers.clear();
    _request_headers.clear();
    _buffer_str.clear();
}

const char* mgHttpClient::method_to_string(HttpMethod method)
{
    switch (method)
    {
        case HTTP_GET: return "GET";
        case HTTP_PUT: return "PUT";
        case HTTP_POST: return "POST";
        case HTTP_DELETE: return "DELETE";
        case HTTP_HEAD: return "HEAD";
        case HTTP_PROPFIND: return "PROPFIND";
        case HTTP_MKCOL: return "MKCOL";
        case HTTP_COPY: return "COPY";
        case HTTP_MOVE: return "MOVE";
        default: return "UNKNOWN";
    }
}

void mgHttpClient::handle_connect(struct mg_connection *c)
{
#ifdef VERBOSE_HTTP
    Debug_printf("mgHttpClient: Connected\n");
#endif
    _transaction_done = false;

    const char *url = _url.c_str();
    struct mg_str host = mg_url_host(url);
    // If url is https://, tell client connection to use TLS
    if (mg_url_is_ssl(url))
    {
        // struct mg_str key_data = mg_file_read(&mg_fs_posix, "tls/private-key.pem");
        struct mg_tls_opts opts = {};
#ifdef SKIP_SERVER_CERT_VERIFY
        opts.ca.ptr = nullptr; // disable certificate checking
#else
        // certificates are loaded on initialization of the client in cross platform way.
        opts.ca = ca;

        // this is how to load the files rather than refer to them by name (for BUILT_IN tls)
        // opts.cert = mg_file_read(&mg_fs_posix, "tls/cert.pem");
        // opts.key = mg_file_read(&mg_fs_posix, "tls/private-key.pem");
#endif
        opts.name = host;
        mg_tls_init(c, &opts);
    }

    // reset response status code
    _status_code = -1;

    // get authentication from url, if any provided
    if (mg_url_user(url).len != 0)
    {
        struct mg_str u = mg_url_user(url);
        struct mg_str p = mg_url_pass(url);
        _username = std::string(u.buf, u.len);
        _password = std::string(p.buf, p.len);
    }

    // Send request
    const char* method_str = method_to_string(_method);
    switch(_method)
    {
        case HTTP_GET:
        case HTTP_PUT:
        case HTTP_POST:
        case HTTP_DELETE:
        case HTTP_HEAD:
        case HTTP_PROPFIND:
        case HTTP_MKCOL:
        case HTTP_COPY:
        case HTTP_MOVE:
        {
            // start the request
            mg_printf(c, "%s %s HTTP/1.1\r\n"
                            "Host: %.*s\r\n"
                            "Connection: close\r\n",
                            method_str, mg_url_uri(url), (int)host.len, host.buf);

            // send auth header
            if (!_username.empty())
                mg_http_bauth(c, _username.c_str(), _password.c_str());

            // send custom headers
            if (_request_headers.size() > 0)
            {
#ifdef VERBOSE_HTTP
                Debug_println("Custom headers");
                for (const auto& rh: _request_headers)
                    Debug_printf("  %s: %s\n", rh.first.c_str(), rh.second.c_str());
#endif
                for (const auto& rh: _request_headers)
                    mg_printf(c, "%s: %s\r\n", rh.first.c_str(), rh.second.c_str());
            }

            // send request body data if any
            if (_post_data != nullptr && _method != HTTP_GET && _method != HTTP_HEAD)
            {
                // Content-Type if none set
                header_map_t::iterator it = _request_headers.find("Content-Type");
                if (it == _request_headers.end())
                    mg_printf(c, "Content-Type: application/octet-stream\r\n");
                // Content-Length
                mg_printf(c, "Content-Length: %d\r\n", _post_datalen);
                mg_printf(c, "\r\n");
                // send request body data
                mg_send(c, _post_data, _post_datalen);
            }
            else
            {
                mg_printf(c, "\r\n");
            }
            break;
        }
        default:
        {
            Debug_printf("mgHttpClient: method %d is not implemented\n", _method);
        }
    }
}


void mgHttpClient::handle_read(struct mg_connection *c)
{
#ifdef VERBOSE_HTTP
    Debug_printf("mgHttpClient: handle_read\n");
    Debug_printf("  Received: %lu\n", c->recv.len);
#endif

    if (_transaction_begin)
    {
        // Waiting for all headers to arrive
        struct mg_http_message hm;
        int hdrs_len = mg_http_parse((char *) c->recv.buf, c->recv.len, &hm);
        if (hdrs_len < 0)
        {
            Debug_println("mgHttpClient: Bad response");
            c->is_draining = 1;
            c->recv.len = 0;
            return;
        }
#ifdef VERBOSE_HTTP
        if (hdrs_len == 0)
        {
            // Not all headers received yet, keep mongoose collecting more data for us
            Debug_println("  need more data");
        }
#endif
        if (hdrs_len > 0)
        {
            // We received all headers
            process_response_headers(c, hm, hdrs_len);
            _transaction_begin = false; // indicate the headers are processed
            _processed = true; // stop polling, headers are available
        }
    }

    // Append body data to buffer, if any
    if (!_transaction_begin && c->recv.len > 0)
    {
        process_body_data(c, (char *)c->recv.buf, c->recv.len);
    }
}

void mgHttpClient::process_response_headers(struct mg_connection *c, struct mg_http_message &hm, int hdrs_len)
{
    _status_code = mg_http_status(&hm);
    _content_length = (int)hm.body.len;
    struct mg_str *te;

    if ((te = mg_http_get_header(&hm, "Transfer-Encoding")) != nullptr)
    {
        if (mg_casecmp(te->buf, "chunked") == 0)
        {
            _is_chunked = true;
        }
        else
        {
            Debug_println("mgHttpClient: Invalid Transfer-Encoding");
        }
    }
    else
    {
        if (_status_code >= 200 && _status_code != 204 && _status_code != 304 && mg_http_get_header(&hm, "Content-length") == nullptr)
        {
            Debug_println("mgHttpClient: No Content-Length header");
        }
    }

#ifdef VERBOSE_HTTP
    Debug_printf("  Headers: %d bytes\n", hdrs_len);
    Debug_printf("  status_code: %d\n", _status_code);
    Debug_printf("  content_length: %d\n", _content_length);
    Debug_printf("  is_chunked: %d\n", _is_chunked);
    //Debug_printf("  Headers data:\n%.*s", (int) hdrs_len, c->recv.buf);  // Print headers
#endif

    // Remember Location on redirect response
    if (_status_code == 301 || _status_code == 302)
    {
        struct mg_str *loc = mg_http_get_header(&hm, "Location");
        if (loc != nullptr)
            _location = std::string(loc->buf, loc->len);
    }
    // Store response headers client is interested in, if any
    else if (_stored_headers.size() > 0)
    {
        size_t max_headers = sizeof(hm.headers) / sizeof(hm.headers[0]);
        for (int i = 0; i < max_headers && hm.headers[i].name.len > 0; i++)
        {
            set_header_value(&hm.headers[i].name, &hm.headers[i].value);
        }
    }

    // Remove headers from mongoose buffer
    if (hdrs_len < c->recv.len)
    {
        memmove(c->recv.buf, c->recv.buf + hdrs_len, (size_t)(c->recv.len - hdrs_len));
        c->recv.len -= hdrs_len;
    }
    else
    {
        c->recv.len = 0;
    }
}

// from mongoose.c
static bool is_hex_digit(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

// from mongoose.c
static int skip_chunk(const char *buf, int len, int *pl, int *dl)
{
    int i = 0, n = 0;
    if (len < 3) return 0;
    while (i < len && is_hex_digit(buf[i])) i++;
    if (i == 0) return -1;                     // Error, no length specified
    if (i > (int) sizeof(int) * 2) return -1;  // Chunk length is too big
    if (len < i + 1 || buf[i] != '\r' || buf[i + 1] != '\n') return -1;  // Error
    n = (int) strtoul(buf, NULL, 16);  // Decode chunk length
    if (n < 0) return -1;                  // Error
    if (n > len - i - 4) return 0;         // Chunk not yet fully buffered
    if (buf[i + n + 2] != '\r' || buf[i + n + 3] != '\n') return -1;  // Error
    *pl = i + 2, *dl = n;
    return i + 2 + n + 2;
}

void mgHttpClient::process_body_data(struct mg_connection *c, char *data, int len)
{
#ifdef VERBOSE_HTTP
        Debug_printf("  Body: %d bytes\n", len);
        //Debug_printf("  Body data:\n%.*s\n", len, data);  // Print body
#endif
    if (_is_chunked)
    {
        int o = 0, l = 0, pl, dl, cl;
        // Get all complete chunks out of mongoose buffer
        while ((cl = skip_chunk(data + o, len - o, &pl, &dl)) > 0)
        {
            // Append chunks data to our buffer
            if (dl > 0)
            {
                _buffer_str.append(data + o + pl, dl);
            }
            o += cl;
        }
        if (o > 0)
        {
            // Remove chunks from mongoose buffer
            if (o < len)
            {
                memmove(data, data + o, (size_t)(len - o));
                c->recv.len -= o;
            }
            else
            {
                c->recv.len = 0;
            }
            _processed = true; // stop polling, data is available in _buffer_str
        }
        if (cl < 0)
        {
            Debug_println("mgHttpClient: Invalid chunk");
            c->is_draining = 1;
            c->recv.len = 0;
            return;
        }
    }
    else
    {
        // Append entire body data to buffer
        _buffer_str.append(data, len);
        c->recv.len = 0;   // cleanup mongoose receive buffer
        _processed = true; // stop polling, data is available in _buffer_str
    }
}

void report_unhandled(int ev)
{
#ifdef VERBOSE_HTTP
    switch(ev)
    {
    case MG_EV_TLS_HS:
        Debug_printf("mgHttpClient: TLS Handshake succeeded\n");
        break;

    case MG_EV_HTTP_HDRS:
        Debug_printf("mgHttpClient: HTTP Headers received\n");
        break;

    case MG_EV_OPEN:
        Debug_printf("mgHttpClient: Open received\n");
        break;

    case MG_EV_WRITE:
        Debug_printf("mgHttpClient: Data written\n");
        break;

    case MG_EV_RESOLVE:
        Debug_printf("mgHttpClient: Host name resolved\n");
        break;

    default:
        Debug_printf("mgHttpClient: Unknown code: %d\n", ev);
        break;

    }
#endif
}

/*
 Typical event order:

 HTTP_EVENT_HANDLER_ON_CONNECTED
 HTTP_EVENT_HEADERS_SENT
 HTTP_EVENT_ON_HEADER - once for each header received with header_key and header_value set
 HTTP_EVENT_ON_DATA - multiple times with data and datalen set up to BUFFER size
 HTTP_EVENT_ON_FINISH - value is returned to esp_http_client_perform() after this
 HTTP_EVENT_DISCONNECTED

 The return value is discarded.
*/
void mgHttpClient::_httpevent_handler(struct mg_connection *c, int ev, void *ev_data)
{
    // // Our user_data should be a pointer to our mgHttpClient object
    mgHttpClient *client = (mgHttpClient *)c->fn_data;
    bool progress = true;

    switch (ev)
    {
    case MG_EV_CONNECT:
        client->handle_connect(c);
        break;

    case MG_EV_READ:
        client->handle_read(c);
        break;

    case MG_EV_CLOSE:
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient: Connection closed\n");
#endif
        client->_transaction_done = true;
        break;

    case MG_EV_ERROR:
        Debug_printf("mgHttpClient: Error - %s\n", (const char*)ev_data);
        client->_transaction_done = true;
        client->_status_code = 901; // Fake HTTP status code to indicate connection error
        break;

    case MG_EV_POLL:
        progress = false;
        break;

    default:
        report_unhandled(ev);
        break;

    }

    client->_progressed = progress; // something is happening, tell event loop to reset timeout watch
}

/*
 Performs an HTTP transaction
*/
int mgHttpClient::_perform()
{
#ifdef VERBOSE_HTTP
    Debug_printf("%08lx _perform\n", (unsigned long)fnSystem.millis());
#endif

    // We want to process the response body (if any)
    // _ignore_response_body = false;

    _redirect_count = 0;
    bool done = false;

    _perform_connect();
    while (!done)
    {
        _perform_fetch(); // process up until we have all headers
        // check the response code
        if (_status_code == 301 || _status_code == 302)
            done = !_perform_redirect(); // continue if we're going to redirect
        else
            done = true;
    }

    // Reset request data
    _post_data = nullptr;
    _post_datalen = 0;

#ifdef VERBOSE_HTTP
    Debug_printf("%08lx _perform status = %d, length = %d, chunked = %d\n", (unsigned long)fnSystem.millis(), _status_code, _content_length, _is_chunked ? 1 : 0);
#endif
    return _status_code;
}

/*
 Initiate HTTP connection
 */
void mgHttpClient::_perform_connect()
{
    _status_code = -1;
    _content_length = 0;
    _is_chunked = false;

    _transaction_begin = true; // waiting for response headers
    _transaction_done = false;

    if (_handle == nullptr)
    {
        _transaction_done = true;
        _status_code = 900; // Fake HTTP status code to indicate general error
        return;
    }

    mg_connect(_handle.get(), _url.c_str(), _httpevent_handler, this);  // Create client connection
}

void mgHttpClient::_perform_fetch()
{
    _processed = false;
    _progressed = false;
    uint64_t ms_update = fnSystem.millis();

    if (_handle == nullptr)
    {
        _transaction_done = true;
        _status_code = 900; // Fake HTTP status code to indicate general error
        return;
    }

    while (true)
    {
        mg_mgr_poll(_handle.get(), 50);

        if (_processed || _transaction_done)
            break; // header and/or body data processed, or transaction done

        if (_progressed)
        {
            // Prepare for next poll
            _progressed = false;
            ms_update = fnSystem.millis();
        }
        else if ((fnSystem.millis() - ms_update) > HTTP_CLIENT_TIMEOUT)
        {
            // No progress, timeout
            Debug_printf("Timed-out waiting for HTTP data\n");
            _transaction_done = true;
            _status_code = 408; // 408 Request Timeout
            break;
        }
    }
}

// Handle HTTP redirect response
bool mgHttpClient::_perform_redirect()
{
    // throw away the current response
    _flush_response();

    if (++_redirect_count > _max_redirects)
    {
        Debug_printf("HTTP redirect (%d) over max allowed redirects (%d)!\n", _redirect_count, _max_redirects);
        _transaction_done = true;
        return false;
    }

    if (_location.empty())
    {
        Debug_printf("HTTP redirect (%d) without Location specified!\n", _redirect_count);
        _transaction_done = true;
        return false;
    }

    Debug_printf("HTTP redirect (%d) to %s\n", _redirect_count, _location.c_str());
    // update url to connect to
    _url = _location;
    _location.clear();
    // create new connection
    _perform_connect();

    return true;
}

void mgHttpClient::_flush_response()
{
    while (!_transaction_done)
    {
        _perform_fetch();
    }
    _buffer_str.clear();
}

int mgHttpClient::PUT(const char *put_data, int put_datalen)
{
#ifdef VERBOSE_HTTP
    Debug_println("mgHttpClient::PUT");
#endif
    if (_handle == nullptr || put_data == nullptr || put_datalen < 1)
        return -1;

    // Get rid of any pending data
    _flush_response();

    // Set method
    _method = HTTP_PUT;
    // Set the content of the body
    set_post_data(put_data, put_datalen);

    return _perform();
}

int mgHttpClient::PROPFIND(webdav_depth depth, const char *properties_xml)
{
#ifdef VERBOSE_HTTP
    Debug_println("mgHttpClient::PROPFIND");
#endif
    if (_handle == nullptr)
        return -1;

    // Get rid of any pending data
    _flush_response();

    // Set method
    _method = HTTP_PROPFIND;
    // Assume any request body will be XML
    set_header("Content-Type", "text/xml");
    // Set depth
    const char *pDepth = webdav_depths[0];
    if (depth == DEPTH_1)
        pDepth = webdav_depths[1];
    else if (depth == DEPTH_INFINITY)
        pDepth = webdav_depths[2];
    set_header("Depth", pDepth);

    // Set the content of the body
    if (properties_xml != nullptr)
        set_post_data(properties_xml, strlen(properties_xml));

    return _perform();
}

int mgHttpClient::DELETE()
{
#ifdef VERBOSE_HTTP
    Debug_println("mgHttpClient::DELETE");
#endif
    if (_handle == nullptr)
        return -1;

    // Get rid of any pending data
    _flush_response();

    // Set method
    _method = HTTP_DELETE;

    return _perform();
}

int mgHttpClient::MKCOL()
{
#ifdef VERBOSE_HTTP
    Debug_println("mgHttpClient::MKCOL");
#endif
    if (_handle == nullptr)
        return -1;

    // Get rid of any pending data
    _flush_response();

    // Set method
    _method = HTTP_MKCOL;

    return _perform();
}

int mgHttpClient::COPY(const char *destination, bool overwrite, bool move)
{
#ifdef VERBOSE_HTTP
    Debug_println("mgHttpClient::COPY");
#endif
    if (_handle == nullptr || destination == nullptr)
        return -1;

    // Get rid of any pending data
    _flush_response();

    // Set method
    _method = HTTP_MOVE;
    // Set detination
    set_header("Destination", destination);
    // Set overwrite
    set_header("Overwrite", overwrite ? "T" : "F");

    return _perform();
}

int mgHttpClient::MOVE(const char *destination, bool overwrite)
{
#ifdef VERBOSE_HTTP
    Debug_println("mgHttpClient::MOVE");
#endif
    return COPY(destination, overwrite, true);
}

/*
 Execute an HTTP POST against current URL. Returns HTTP result code
 By default, <Content-Type> is set to <application/x-www-form-urlencoded>
 and data pointed to by post_data should be, also.  This can be overriden by
 setting the appropriate content type using set_header().
 <Content-Length> is set based on post_datalen.
*/
int mgHttpClient::POST(const char *post_data, int post_datalen)
{
#ifdef VERBOSE_HTTP
    Debug_println("mgHttpClient::POST");
#endif
    if (_handle == nullptr || post_data == nullptr || post_datalen < 1)
        return -1;

    // Get rid of any pending data
    _flush_response();

    // Set method
    _method = HTTP_POST;
    // Set the content of the body
    set_post_data(post_data, post_datalen);

    return _perform();
}

// Execute an HTTP GET against current URL.  Returns HTTP result code
int mgHttpClient::GET()
{
#ifdef VERBOSE_HTTP
    Debug_println("mgHttpClient::GET");
#endif
    if (_handle == nullptr)
        return -1;

    // Get rid of any pending data
    _flush_response();

    // Set method
    _method = HTTP_GET;

    return _perform();
}

int mgHttpClient::HEAD()
{
#ifdef VERBOSE_HTTP
    Debug_println("mgHttpClient::HEAD");
#endif
    if (_handle == nullptr)
        return -1;

    // Get rid of any pending data
    _flush_response();

    // Set method
    _method = HTTP_HEAD;
    return _perform();
}

// Sets the URL for the next HTTP request
// Existing connection will be closed if this is a different host
bool mgHttpClient::set_url(const char *url)
{
    if (_handle == nullptr)
        return false;

    _url = std::string(url);

    return true;
}

bool mgHttpClient::set_post_data(const char *post_data, int post_datalen)
{
    _post_data = post_data;
    _post_datalen = (post_data == nullptr) ? 0 : post_datalen;

    return true;
}

// Sets an HTTP request header
bool mgHttpClient::set_header(const char *header_key, const char *header_value)
{
    if (_handle == nullptr || header_key == nullptr || header_value == nullptr)
        return false;

    if (_request_headers.size() >= 20)
        return false;

    _request_headers[header_key] = header_value;
    return true;
}

// Returns number of response headers available to read
int mgHttpClient::get_header_count()
{
    return _stored_headers.size();
}

char *mgHttpClient::get_header(int index, char *buffer, int buffer_len)
{
    if (index < 0 || index > (_stored_headers.size() - 1))
        return nullptr;

    if (buffer == nullptr)
        return nullptr;

    auto vi = _stored_headers.begin();
    std::advance(vi, index);
    return strncpy(buffer, vi->second.c_str(), buffer_len);
}

const std::string mgHttpClient::get_header(int index)
{
    if (index < 0 || index > (_stored_headers.size() - 1))
        return std::string();

    auto vi = _stored_headers.begin();
    std::advance(vi, index);
    return vi->second;
}

// Returns value of requested response header or empty string if there is no match
const std::string mgHttpClient::get_header(const char *header)
{
    std::string hkey = util_tolower(header);
    header_map_t::iterator it = _stored_headers.find(hkey);
    if (it != _stored_headers.end())
        return it->second;
    return std::string();
}

// Specifies names of response headers to be stored from the server response
void mgHttpClient::create_empty_stored_headers(const std::vector<std::string>& headerKeys)
{
    if (_handle == nullptr || headerKeys.empty())
        return;

    _stored_headers.clear();
    for (const auto& key : headerKeys) {
        std::string lower_key = util_tolower(key);
        _stored_headers[lower_key] = std::string();
    }
}

// Sets the header's value in the map if found (case insensitive)
void mgHttpClient::set_header_value(const struct mg_str *name, const struct mg_str *value)
{
    std::string hkey = util_tolower(std::string(name->buf, name->len));
    header_map_t::iterator it = _stored_headers.find(hkey);
    if (it != _stored_headers.end())
    {
        std::string hval(std::string(value->buf, value->len));
        it->second = hval;
    }
}

#endif // !ESP_PLATFORM
