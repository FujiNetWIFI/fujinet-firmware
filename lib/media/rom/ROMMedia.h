#ifndef ROMMEDIA_H
#define ROMMEDIA_H

#include "media.h"

class ROMMedia : public Media
{
protected:
    size_t _size = 0;

    // Sets _size from the file; an error if the length can't be read.
    error_is_true readSize();

public:
    ROMMedia(const std::string &path, fnFile *handle) : Media(path, handle) {}

    bool isReadOnly() const override { return true; }

    // Length in bytes of what read() returns: the whole file, header included.
    virtual size_t size() const { return _size; }

    // Returns the next chunk of the image; a short or empty buffer means
    // the end. An error is reserved for I/O failure.
    virtual size_result_t read(ByteBuffer &buffer, size_t length);

    error_is_true close() override;
};

#endif /* ROMMEDIA_H */
