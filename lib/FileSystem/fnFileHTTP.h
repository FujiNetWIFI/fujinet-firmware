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
 * FileHandlerHTTP - read-only FileHandler that streams a remote resource with
 * HTTP Range requests instead of caching the whole file.
 *
 * seek() just moves a logical cursor; read() fetches a bounded window
 * (bytes=lo-hi) at the cursor into a read-ahead buffer, so sequential reads
 * share one request and only the window is ever held in memory. Ranges must be
 * bounded, not open-ended: the PC client downloads the whole body during GET().
 *
 * create() returns nullptr unless the server advertises range support and a
 * content length, so the caller can fall back to caching.
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
