
#include "fnFileHTTP.h"

#ifndef FNIO_IS_STDIO

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../../include/debug.h"
#include "string_utils.h"

// Bytes fetched per Range request: big enough to amortize the TLS handshake over
// sequential reads, small enough to keep memory bounded.
#define HTTP_STREAM_WINDOW 65536

static uint8_t *stream_buf_alloc(size_t len)
{
#ifdef ESP_PLATFORM
    return (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return (uint8_t *)malloc(len);
#endif
}

FileHandlerHTTP::FileHandlerHTTP(const std::string &url, long size)
    : _url(url), _size(size), _position(0), _client(nullptr),
      _buf(nullptr), _buf_start(-1), _buf_len(0), _eof(false)
{
    _buf = stream_buf_alloc(HTTP_STREAM_WINDOW);
}

FileHandlerHTTP::~FileHandlerHTTP()
{
    if (_client != nullptr)
        delete _client;
    if (_buf != nullptr)
        free(_buf);
}

// Probe the URL for byte-range support and determine its size.
FileHandlerHTTP *FileHandlerHTTP::create(const std::string &url)
{
    long size = -1;
    bool ranges = false;

    // --- HEAD probe: look for Accept-Ranges + Content-Length ---
    HTTP_CLIENT_CLASS *client = new HTTP_CLIENT_CLASS();
    if (client == nullptr)
        return nullptr;

    if (client->begin(url))
    {
        std::vector<std::string> wanted = {"Accept-Ranges", "Content-Length"};
        client->create_empty_stored_headers(wanted);
        int rc = client->HEAD();
        if (rc >= 200 && rc < 400)
        {
            std::string ar = client->get_header("Accept-Ranges");
            std::string cl = client->get_header("Content-Length");
            mstr::toLower(ar);
            if (ar.find("bytes") != std::string::npos)
                ranges = true;
            if (!cl.empty())
                size = atol(cl.c_str());
        }
    }
    delete client;
    client = nullptr;

    // --- Fallback probe: a bytes=0-0 GET; a 206 + Content-Range confirms ranges ---
    if (!ranges || size < 0)
    {
        client = new HTTP_CLIENT_CLASS();
        if (client == nullptr)
            return nullptr;

        if (client->begin(url))
        {
            std::vector<std::string> wanted = {"Content-Range", "Content-Length"};
            client->create_empty_stored_headers(wanted);
            client->set_header("Range", "bytes=0-0");
            int rc = client->GET();
            if (rc == 206)
            {
                ranges = true;
                // Content-Range: "bytes 0-0/12345"  (total after '/')
                std::string cr = client->get_header("Content-Range");
                size_t slash = cr.find('/');
                if (slash != std::string::npos)
                {
                    std::string total = cr.substr(slash + 1);
                    if (!total.empty() && total[0] != '*')
                        size = atol(total.c_str());
                }
            }
        }
        delete client;
        client = nullptr;
    }

    // Streaming needs both range support and a known size (SEEK_END / filesize()).
    if (!ranges || size < 0)
    {
        Debug_printf("FileHandlerHTTP::create - no usable range support for \"%s\" (ranges=%d, size=%ld)\r\n",
                     url.c_str(), ranges, size);
        return nullptr;
    }

    Debug_printf("FileHandlerHTTP::create - streaming \"%s\", size=%ld\r\n", url.c_str(), size);
    return new FileHandlerHTTP(url, size);
}

// Issue one bounded ranged GET on the (reused) client, reading the body into _buf.
int FileHandlerHTTP::fetch_range(long pos, long want, long *out_total)
{
    *out_total = 0;

    // Bounded range: fetch exactly [pos, pos+want-1]. Open-ended ranges would
    // make the PC client download the rest of the file during GET().
    char range[48];
    snprintf(range, sizeof(range), "bytes=%ld-%ld", pos, pos + want - 1);
    _client->set_header("Range", range);

    int rc = _client->GET();
    if (rc != 200 && rc != 206)
        return rc;

    // 200 means the server ignored the Range (body starts at 0); discard the
    // leading bytes. A safety net only - create() already confirmed range support.
    long skip = (rc == 200) ? pos : 0;
    uint8_t scratch[512];
    while (skip > 0)
    {
        int chunk = skip < (long)sizeof(scratch) ? (int)skip : (int)sizeof(scratch);
        int got = _client->read(scratch, chunk);
        if (got <= 0)
            return -1;
        skip -= got;
    }

    // Read the window body into _buf.
    long total = 0;
    while (total < want)
    {
        int got = _client->read(_buf + total, (int)(want - total));
        if (got <= 0)
            break;
        total += got;
    }

    *out_total = total;
    return rc;
}

// Fetch a bounded window of the resource into _buf, starting at absolute pos.
// The client (and its connection) is reused across windows via keep-alive; a
// dropped keep-alive connection is retried once on a fresh client.
bool FileHandlerHTTP::fill_window(long pos)
{
    _buf_start = -1;
    _buf_len = 0;

    if (_buf == nullptr || pos < 0 || (_size >= 0 && pos >= _size))
        return false;

    long want = HTTP_STREAM_WINDOW;
    if (_size >= 0 && pos + want > _size)
        want = _size - pos;
    if (want <= 0)
        return false;

    for (int attempt = 0; attempt < 2; attempt++)
    {
        bool fresh = false;
        if (_client == nullptr)
        {
            _client = new HTTP_CLIENT_CLASS();
            if (_client == nullptr)
                return false;
            _client->set_keep_alive(true);
            if (!_client->begin(_url))
            {
                delete _client;
                _client = nullptr;
                return false;
            }
            fresh = true;
        }

        long total = 0;
        int rc = fetch_range(pos, want, &total);

        if ((rc == 200 || rc == 206) && total > 0)
        {
            _buf_start = pos;
            _buf_len = total;
#ifdef ESP_PLATFORM
            // The ESP client is used one request per instance; don't reuse it.
            delete _client;
            _client = nullptr;
#endif
            return true;
        }

        // Failed. Drop the client so the next attempt reconnects fresh.
        delete _client;
        _client = nullptr;

        if (rc == 416)
        {
            _eof = true; // requested past end
            return false;
        }
        if (fresh)
            return false; // a fresh connection genuinely failed; don't loop forever
        // Otherwise the reused keep-alive connection was stale: retry fresh.
    }

    return false;
}

size_t FileHandlerHTTP::read(void *ptr, size_t size, size_t count)
{
    if (size == 0 || count == 0 || ptr == nullptr)
        return 0;

    size_t want = size * count;
    size_t got = 0;
    uint8_t *out = (uint8_t *)ptr;

    while (got < want)
    {
        // (Re)fill the window when the cursor falls outside the buffered range.
        if (_buf_start < 0 || _position < _buf_start || _position >= _buf_start + _buf_len)
        {
            if (!fill_window(_position))
            {
                if (_size >= 0 && _position >= _size)
                    _eof = true;
                break;
            }
        }

        long off = _position - _buf_start;
        long avail = _buf_len - off;
        size_t n = (want - got) < (size_t)avail ? (want - got) : (size_t)avail;
        memcpy(out + got, _buf + off, n);
        got += n;
        _position += n;
    }

    return got / size; // number of complete elements read (fread semantics)
}

size_t FileHandlerHTTP::write(const void *ptr, size_t size, size_t count)
{
    // HTTP host is read-only.
    return 0;
}

int FileHandlerHTTP::seek(long int off, int whence)
{
    long new_pos;
    switch (whence)
    {
    case SEEK_SET:
        new_pos = off;
        break;
    case SEEK_CUR:
        new_pos = _position + off;
        break;
    case SEEK_END:
        if (_size < 0)
        {
            errno = EINVAL;
            return -1;
        }
        new_pos = _size + off;
        break;
    default:
        Debug_printf("FileHandlerHTTP::seek - invalid whence: %d\r\n", whence);
        errno = EINVAL;
        return -1;
    }

    if (new_pos < 0)
    {
        errno = EINVAL;
        return -1;
    }

    // Purely logical - the next read() fetches the window if needed. Keeps
    // filesize()'s seek(END)/seek(0) probe (and in-window seeks) network-free.
    _position = new_pos;
    _eof = false;
    return 0;
}

long int FileHandlerHTTP::tell()
{
    return _position;
}

int FileHandlerHTTP::flush()
{
    return 0;
}

int FileHandlerHTTP::eof()
{
    return _eof ? 1 : 0;
}

int FileHandlerHTTP::close(bool destroy)
{
    if (destroy)
        delete this;
    return 0;
}

#endif // !FNIO_IS_STDIO
