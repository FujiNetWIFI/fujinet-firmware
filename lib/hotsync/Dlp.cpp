#include "Dlp.h"

#include <algorithm>

static constexpr uint8_t DLP_RESPONSE_BIT = 0x80;
static constexpr uint8_t DLP_ARG_SHORT = 0x80;
static constexpr uint8_t DLP_ARG_LONG = 0x40;
static constexpr uint8_t DLP_ARG_ID_MASK = 0x3F;

const char *dlp_error_name(DlpError error)
{
    switch (error)
    {
    case DlpError::NONE: return "no error";
    case DlpError::SYSTEM: return "system error";
    case DlpError::ILLEGAL_REQ: return "unknown function";
    case DlpError::MEMORY: return "out of memory";
    case DlpError::PARAM: return "invalid parameter";
    case DlpError::NOT_FOUND: return "not found";
    case DlpError::NONE_OPEN: return "no open databases";
    case DlpError::DATABASE_OPEN: return "database open elsewhere";
    case DlpError::TOO_MANY_OPEN_DATABASES: return "too many open databases";
    case DlpError::ALREADY_EXISTS: return "already exists";
    case DlpError::CANT_OPEN: return "cannot open database";
    case DlpError::RECORD_DELETED: return "record deleted";
    case DlpError::RECORD_BUSY: return "record busy";
    case DlpError::NOT_SUPPORTED: return "not supported";
    case DlpError::READ_ONLY: return "read only";
    case DlpError::NOT_ENOUGH_SPACE: return "not enough space";
    case DlpError::LIMIT_EXCEEDED: return "size limit exceeded";
    case DlpError::CANCEL_SYNC: return "sync cancelled";
    case DlpError::TRANSPORT: return "link failure";
    }
    return "unknown error";
}

DlpArgWriter &DlpArgWriter::u8(uint8_t v)
{
    _data.push_back(v);
    return *this;
}

DlpArgWriter &DlpArgWriter::u16(uint16_t v)
{
    u16be_t be;
    be = v;
    return bytes(be.bytes, sizeof(be));
}

DlpArgWriter &DlpArgWriter::u32(uint32_t v)
{
    u32be_t be;
    be = v;
    return bytes(be.bytes, sizeof(be));
}

DlpArgWriter &DlpArgWriter::bytes(const uint8_t *data, size_t len)
{
    _data.insert(_data.end(), data, data + len);
    return *this;
}

DlpArgWriter &DlpArgWriter::cstring(const std::string &s)
{
    _data.insert(_data.end(), s.begin(), s.end());
    _data.push_back(0);
    return *this;
}

DlpArgWriter &DlpArgWriter::type_id(const char id[4])
{
    return bytes(reinterpret_cast<const uint8_t *>(id), 4);
}

uint8_t DlpArgReader::u8()
{
    ByteBuffer b = bytes(1);
    return b[0];
}

uint16_t DlpArgReader::u16()
{
    u16be_t be;
    ByteBuffer b = bytes(sizeof(be));
    std::memcpy(&be, b.data(), sizeof(be));
    return be;
}

uint32_t DlpArgReader::u32()
{
    u32be_t be;
    ByteBuffer b = bytes(sizeof(be));
    std::memcpy(&be, b.data(), sizeof(be));
    return be;
}

ByteBuffer DlpArgReader::bytes(size_t len)
{
    if (remaining() < len)
    {
        _failed = true;
        _pos = _data.size();
        return ByteBuffer(len, 0);
    }
    ByteBuffer out(_data.begin() + _pos, _data.begin() + _pos + len);
    _pos += len;
    return out;
}

ByteBuffer DlpArgReader::rest()
{
    return bytes(remaining());
}

std::string DlpArgReader::cstring()
{
    auto begin = _data.begin() + std::min(_pos, _data.size());
    auto end = std::find(begin, _data.end(), 0);
    std::string s(begin, end);
    _pos += s.size() + (end != _data.end() ? 1 : 0);
    return s;
}

void DlpArgReader::skip(size_t len)
{
    bytes(len);
}

DlpRequest &DlpRequest::arg(const DlpArgWriter &writer, uint8_t index)
{
    args.push_back({index, writer.data()});
    return *this;
}

ByteBuffer DlpRequest::encode() const
{
    ByteBuffer out{static_cast<uint8_t>(func), static_cast<uint8_t>(args.size())};
    for (const Arg &a : args)
    {
        uint8_t id = DLP_ARG_ID_BASE + a.index;
        size_t len = a.data.size();
        if (len <= 0xFF)
        {
            out.push_back(id);
            out.push_back(static_cast<uint8_t>(len));
        }
        else if (len <= 0xFFFF)
        {
            u16be_t be;
            be = static_cast<uint16_t>(len);
            out.insert(out.end(), {static_cast<uint8_t>(id | DLP_ARG_SHORT), 0});
            out.insert(out.end(), be.bytes, be.bytes + sizeof(be));
        }
        else
        {
            u32be_t be;
            be = static_cast<uint32_t>(len);
            out.insert(out.end(), {static_cast<uint8_t>(id | DLP_ARG_LONG), 0});
            out.insert(out.end(), be.bytes, be.bytes + sizeof(be));
        }
        out.insert(out.end(), a.data.begin(), a.data.end());
    }
    return out;
}

const ByteBuffer &DlpResponse::arg(size_t index) const
{
    static const ByteBuffer empty;
    return index < args.size() ? args[index] : empty;
}

success_is_true DlpResponse::decode(DlpFunc expected, const ByteBuffer &message)
{
    DlpArgReader reader(message);
    uint8_t func = reader.u8();
    uint8_t argc = reader.u8();
    uint16_t error_code = reader.u16();
    if (reader.failed() || func != (static_cast<uint8_t>(expected) | DLP_RESPONSE_BIT))
        RETURN_ERROR_AS_FALSE();

    error = static_cast<DlpError>(error_code);
    args.clear();
    for (uint8_t i = 0; i < argc; ++i)
    {
        uint8_t id_byte = reader.u8();
        uint32_t len;
        if (id_byte & DLP_ARG_SHORT)
        {
            reader.skip(1);
            len = reader.u16();
        }
        else if (id_byte & DLP_ARG_LONG)
        {
            reader.skip(1);
            len = reader.u32();
        }
        else
        {
            len = reader.u8();
        }
        uint8_t index = (id_byte & DLP_ARG_ID_MASK) - (DLP_ARG_ID_BASE & DLP_ARG_ID_MASK);
        ByteBuffer value = reader.bytes(len);
        if (reader.failed())
            RETURN_ERROR_AS_FALSE();
        if (index >= args.size())
            args.resize(index + 1);
        args[index] = std::move(value);
    }
    RETURN_SUCCESS_AS_TRUE();
}
