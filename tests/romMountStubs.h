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

// How a device last ended a transaction with the computer.
enum class Outcome { None, Success, Error, Data, DataWithError };

// FujiBus packets the systemBus stub was given, and the replies it plays back.
// Also how the device answered the computer through the transaction calls.
struct BusLog
{
    std::vector<Sent> sent;
    std::vector<Reply> replies; // one per packet; ACK once it runs out

    Outcome outcome = Outcome::None;
    std::vector<uint8_t> answer;   // what transaction_send() sent
    std::vector<uint8_t> incoming; // what transaction_get() hands the device

    void reset()
    {
        sent.clear();
        replies.clear();
        outcome = Outcome::None;
        answer.clear();
        incoming.clear();
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
// deletes it and a second close or a use after close is recorded, not a crash.
class MemFile : public FileHandler
{
public:
    explicit MemFile(const std::vector<uint8_t> &image) : _own(image), _image(&_own) {}
    MemFile(const MemFile &) = delete;
    MemFile &operator=(const MemFile &) = delete;

    // A handle on image itself rather than a copy, so a write through one
    // handle shows through every other handle on the same image.
    static std::unique_ptr<MemFile> sharing(std::vector<uint8_t> &image)
    {
        return std::unique_ptr<MemFile>(new MemFile(Shared{}, &image));
    }

    const std::vector<uint8_t> &contents() const { return *_image; }

    int closes = 0;
    bool usedAfterClose = false;

    // A handle opened "rb": every write fails, as it does on a file opened read-only.
    bool readOnly = false;

    int close(bool) override
    {
        closes++;
        return 0;
    }

    int seek(long int off, int whence) override
    {
        touch();
        long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? _pos : (long)_image->size();
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
        size_t left = _pos < (long)_image->size() ? _image->size() - _pos : 0;
        size_t got = want < left ? want : left;
        memcpy(ptr, _image->data() + _pos, got);
        _pos += got;
        return size ? got / size : 0;
    }

    // Grows the image when the write runs past its end, as a file would.
    size_t write(const void *ptr, size_t size, size_t n) override
    {
        touch();
        if (readOnly)
            return 0;
        size_t length = size * n;
        if (_pos + length > _image->size())
            _image->resize(_pos + length);
        memcpy(_image->data() + _pos, ptr, length);
        _pos += length;
        return n;
    }

    int flush() override
    {
        touch();
        return 0;
    }

private:
    struct Shared {};
    MemFile(Shared, std::vector<uint8_t> *image) : _image(image) {}

    void touch()
    {
        if (closes)
            usedAfterClose = true;
    }

    std::vector<uint8_t> _own;
    std::vector<uint8_t> *_image;
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

// What msx_rom_db_open() serves; no database at all when hasDb is false.
struct MsxRomDb
{
    bool hasDb = false;
    std::string text;

    void reset()
    {
        hasDb = false;
        text.clear();
    }
};

extern MsxRomDb msx_rom_db;

#endif // ROMMOUNTSTUBS_H
