#ifndef ROMMOUNTSTUBS_H
#define ROMMOUNTSTUBS_H

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "bus.h"
#include "fnFile.h"

enum class Reply { Ack, Nak, None };

struct Sent
{
    fujiDeviceID_t device;
    fujiCommandID_t command;
    std::vector<uint32_t> params;
    std::string payload;
};

// FujiBus packets the systemBus stub was given, and the replies it plays back.
struct BusLog
{
    std::vector<Sent> sent;
    std::vector<Reply> replies; // one per packet; ACK once it runs out

    void reset()
    {
        sent.clear();
        replies.clear();
    }
};

extern BusLog bus;

// Each 256-byte stretch differs from its neighbours, so a dropped or reordered block shows.
inline std::vector<uint8_t> make_image(size_t length)
{
    std::vector<uint8_t> image(length);
    for (size_t idx = 0; idx < length; idx++)
        image[idx] = (uint8_t)(idx * 7 + idx / 256);
    return image;
}

// An in-memory image that counts closes. Its creator owns it, so close() never
// deletes it and a second close or a read after close is recorded, not a crash.
class MemFile : public FileHandler
{
public:
    explicit MemFile(const std::vector<uint8_t> &image) : _image(image) {}

    int closes = 0;
    bool usedAfterClose = false;

    int close(bool) override
    {
        closes++;
        return 0;
    }

    int seek(long int off, int whence) override
    {
        touch();
        long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? _pos : (long)_image.size();
        if (base + off < 0)
            return -1;
        _pos = base + off;
        return 0;
    }

    long int tell() override
    {
        touch();
        return _pos;
    }

    size_t read(void *ptr, size_t size, size_t n) override
    {
        touch();
        size_t want = size * n;
        size_t left = _pos < (long)_image.size() ? _image.size() - _pos : 0;
        size_t got = want < left ? want : left;
        memcpy(ptr, _image.data() + _pos, got);
        _pos += got;
        return size ? got / size : 0;
    }

    size_t write(const void *, size_t, size_t) override { return 0; }
    int flush() override { return 0; }

private:
    void touch()
    {
        if (closes)
            usedAfterClose = true;
    }

    std::vector<uint8_t> _image;
    long _pos = 0;
};

// Files the fujiHost stub serves by path, every handle it opened on them, and
// the prefix it reports.
struct HostFiles
{
    std::map<std::string, std::vector<uint8_t>> files;
    std::vector<std::unique_ptr<MemFile>> opened;
    std::string prefix;

    void reset()
    {
        files.clear();
        opened.clear();
        prefix.clear();
    }
};

extern HostFiles host_files;

#endif // ROMMOUNTSTUBS_H
