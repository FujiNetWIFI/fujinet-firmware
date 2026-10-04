#ifndef HOTSYNC_DLP_H
#define HOTSYNC_DLP_H

// Desktop Link Protocol message encoding.
// Reference: palm-sync src/protocols/dlp-protocol.ts, Palm OS SDK DLCommon.h

#include "global_types.h"

#include <cstdint>
#include <string>
#include <vector>

enum class DlpFunc : uint8_t {
    ReadUserInfo = 0x10,
    WriteUserInfo = 0x11,
    ReadSysInfo = 0x12,
    GetSysDateTime = 0x13,
    SetSysDateTime = 0x14,
    ReadStorageInfo = 0x15,
    ReadDBList = 0x16,
    OpenDB = 0x17,
    CreateDB = 0x18,
    CloseDB = 0x19,
    DeleteDB = 0x1A,
    ReadAppBlock = 0x1B,
    WriteAppBlock = 0x1C,
    ReadSortBlock = 0x1D,
    WriteSortBlock = 0x1E,
    ReadRecord = 0x20,
    WriteRecord = 0x21,
    ReadResource = 0x23,
    WriteResource = 0x24,
    ResetSystem = 0x29,
    AddSyncLogEntry = 0x2A,
    ReadOpenDBInfo = 0x2B,
    OpenConduit = 0x2E,
    EndOfSync = 0x2F,
};

enum class DlpError : uint16_t {
    NONE = 0,
    SYSTEM = 1,
    ILLEGAL_REQ = 2,
    MEMORY = 3,
    PARAM = 4,
    NOT_FOUND = 5,
    NONE_OPEN = 6,
    DATABASE_OPEN = 7,
    TOO_MANY_OPEN_DATABASES = 8,
    ALREADY_EXISTS = 9,
    CANT_OPEN = 10,
    RECORD_DELETED = 11,
    RECORD_BUSY = 12,
    NOT_SUPPORTED = 13,
    READ_ONLY = 15,
    NOT_ENOUGH_SPACE = 16,
    LIMIT_EXCEEDED = 17,
    CANCEL_SYNC = 18,
    // Not a Palm code: the transport failed before a reply arrived.
    TRANSPORT = 0xFFFF,
};

const char *dlp_error_name(DlpError error);

constexpr uint8_t DLP_ARG_ID_BASE = 0x20;

// Big-endian field writer for one DLP argument.
class DlpArgWriter
{
public:
    DlpArgWriter &u8(uint8_t v);
    DlpArgWriter &u16(uint16_t v);
    DlpArgWriter &u32(uint32_t v);
    DlpArgWriter &bytes(const uint8_t *data, size_t len);
    DlpArgWriter &bytes(const ByteBuffer &data) { return bytes(data.data(), data.size()); }
    DlpArgWriter &cstring(const std::string &s);
    DlpArgWriter &type_id(const char id[4]);

    const ByteBuffer &data() const { return _data; }

private:
    ByteBuffer _data;
};

// Bounds-checked big-endian field reader over one DLP argument. A read past
// the end yields zeros and marks the reader failed.
class DlpArgReader
{
public:
    explicit DlpArgReader(const ByteBuffer &data) : _data(data) {}

    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    ByteBuffer bytes(size_t len);
    ByteBuffer rest();
    std::string cstring();
    void skip(size_t len);

    size_t remaining() const { return _pos < _data.size() ? _data.size() - _pos : 0; }
    bool failed() const { return _failed; }

private:
    const ByteBuffer &_data;
    size_t _pos = 0;
    bool _failed = false;
};

struct DlpRequest {
    struct Arg {
        uint8_t index;
        ByteBuffer data;
    };

    DlpFunc func;
    std::vector<Arg> args;

    explicit DlpRequest(DlpFunc f) : func(f) {}
    // Arguments are numbered from DLP_ARG_ID_BASE; a few requests use index 1.
    DlpRequest &arg(const DlpArgWriter &writer, uint8_t index = 0);
    ByteBuffer encode() const;
};

struct DlpResponse {
    DlpError error = DlpError::TRANSPORT;
    // Indexed by argument ID minus DLP_ARG_ID_BASE; absent arguments are empty.
    std::vector<ByteBuffer> args;

    bool ok() const { return error == DlpError::NONE; }
    const ByteBuffer &arg(size_t index) const;
    success_is_true decode(DlpFunc expected, const ByteBuffer &message);
};

#endif // HOTSYNC_DLP_H
