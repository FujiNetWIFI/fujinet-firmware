#ifndef ROMMEDIA_H
#define ROMMEDIA_H

#include "media.h"

#include <cstdint>
#include <memory_resource>
#include <optional>
#include <string>
#include <vector>

class fujiHost;

class ROMMedia : public Media
{
protected:
    fujiHost *_host;
    size_t _size = 0;

    // Sets _size from the file; an error if the length can't be read.
    error_is_true readSize();

public:
    // host lets a format look for a sibling file, such as a mapper, until paths
    // become URLs; may be nullptr.
    ROMMedia(const std::string &path, fnFile *handle, fujiHost *host)
        : Media(path, handle), _host(host) {}

    bool isReadOnly() const override { return true; }

    // Length in bytes of what read() returns: the whole file, header included.
    virtual size_t size() const { return _size; }

    // Returns the next chunk of the image; a short or empty buffer means
    // the end. An error is reserved for I/O failure.
    virtual size_result_t read(ByteBuffer &buffer, size_t length);

    // Mapping information for the cart, passed through to the device verbatim;
    // nullopt if the format has none.
    virtual std::optional<std::pmr::vector<uint8_t>> mapperInfo() { return std::nullopt; }

    error_is_true close() override;
};

#endif /* ROMMEDIA_H */
