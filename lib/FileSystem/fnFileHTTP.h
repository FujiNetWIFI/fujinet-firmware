#ifndef FN_FILEHTTP_H
#define FN_FILEHTTP_H

#include "fnio.h"

#ifndef FNIO_IS_STDIO

#include <stdint.h>
#include <string>

#include "fnFile.h"

#ifdef ESP_PLATFORM
#include "fnHttpClient.h"
#ifndef HTTP_CLIENT_CLASS
#define HTTP_CLIENT_CLASS fnHttpClient
#endif
#else
#include "mgHttpClient.h"
#ifndef HTTP_CLIENT_CLASS
#define HTTP_CLIENT_CLASS mgHttpClient
#endif
#endif

/*
 * FileHandlerHTTP - read-only FileHandler that streams a remote resource over
 * HTTP instead of caching the whole thing locally.
 *
 * Random access (POINT/NOTE, disk sector reads) is served with bounded HTTP
 * Range requests. seek() only moves a logical cursor; read() fetches a window
 * of bytes starting at that cursor (bytes=lo-hi) into a small read-ahead
 * buffer, so a run of sequential reads is satisfied from one request. Only a
 * fixed-size window is ever held in memory, never the whole file.
 *
 * Bounded (not open-ended) ranges are required because the PC HTTP client
 * downloads the entire response body during GET(); an open-ended bytes=N-
 * would pull the rest of the file in one shot.
 *
 * Only usable when the server advertises byte-range support AND a content
 * length; create() returns nullptr otherwise so the caller can fall back to
 * downloading the file to a cache.
 */
class FileHandlerHTTP : public FileHandler
{
protected:
    std::string _url;   // fully-qualified, url-encoded resource URL
    long _size;         // total resource size (always known when constructed)
    long _position;     // logical read cursor

    HTTP_CLIENT_CLASS *_client; // persistent keep-alive client reused across windows

    uint8_t *_buf;      // read-ahead window buffer
    long _buf_start;    // absolute offset of _buf[0], or -1 when empty
    long _buf_len;      // valid bytes in _buf
    bool _eof;

private:
    // Fetch a window of data starting at absolute offset pos into _buf via a
    // bounded HTTP Range request. Returns false on EOF/error.
    bool fill_window(long pos);
    // Issue one bounded ranged GET on _client into _buf. Returns the HTTP status
    // and sets *out_total to the number of body bytes read.
    int fetch_range(long pos, long want, long *out_total);

public:
    FileHandlerHTTP(const std::string &url, long size);
    virtual ~FileHandlerHTTP() override;

    // Probe url for byte-range support. Returns a new handler on success, or
    // nullptr when the server can't serve ranges / size is unknown (caller
    // should fall back to caching the whole file).
    static FileHandlerHTTP *create(const std::string &url);

    virtual int close(bool destroy = true) override;
    virtual int seek(long int off, int whence) override;
    virtual long int tell() override;
    virtual size_t read(void *ptr, size_t size, size_t count) override;
    virtual size_t write(const void *ptr, size_t size, size_t count) override;
    virtual int flush() override;
    virtual int eof() override;
};

#endif // !FNIO_IS_STDIO

#endif // FN_FILEHTTP_H
