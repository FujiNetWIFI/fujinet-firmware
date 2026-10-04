#include "Slp.h"

#include <algorithm>
#include <cstddef>
#include <iterator>

static constexpr uint8_t SLP_SIGNATURE[3] = {0xBE, 0xEF, 0xED};
static constexpr size_t SLP_CRC_LENGTH = 2;

uint16_t slp_crc16(const uint8_t *data, size_t len, uint16_t crc)
{
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

static uint8_t header_checksum(const SlpHeader &header)
{
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&header);
    uint8_t sum = 0;
    for (size_t i = 0; i < offsetof(SlpHeader, checksum); ++i)
        sum += bytes[i];
    return sum;
}

ByteBuffer slp_encode(const SlpDatagram &datagram)
{
    SlpHeader header;
    std::copy(std::begin(SLP_SIGNATURE), std::end(SLP_SIGNATURE), header.signature);
    header.dest_socket = datagram.dest_socket;
    header.src_socket = datagram.src_socket;
    header.type = static_cast<uint8_t>(datagram.type);
    header.data_length = static_cast<uint16_t>(datagram.payload.size());
    header.xid = datagram.xid;
    header.checksum = header_checksum(header);

    const uint8_t *header_bytes = reinterpret_cast<const uint8_t *>(&header);
    ByteBuffer frame(header_bytes, header_bytes + sizeof(header));
    frame.insert(frame.end(), datagram.payload.begin(), datagram.payload.end());

    u16be_t crc;
    crc = slp_crc16(frame.data(), frame.size());
    frame.insert(frame.end(), crc.bytes, crc.bytes + sizeof(crc));
    return frame;
}

// The timeout is an idle timeout: it restarts whenever bytes arrive.
success_is_true SlpReader::read(SlpDatagram &out, uint32_t timeout_ms)
{
    for (;;)
    {
        if (try_parse(out).is_success())
            RETURN_SUCCESS_AS_TRUE();
        if (fill(timeout_ms).is_error())
            RETURN_ERROR_AS_FALSE();
    }
}

success_is_true SlpReader::fill(uint32_t timeout_ms)
{
    uint8_t chunk[256];
    int got = _link.read(chunk, sizeof(chunk), timeout_ms);
    if (got < 0)
        _closed = true;
    if (got <= 0)
        RETURN_ERROR_AS_FALSE();
    _pending.insert(_pending.end(), chunk, chunk + got);
    RETURN_SUCCESS_AS_TRUE();
}

success_is_true SlpReader::try_parse(SlpDatagram &out)
{
    for (;;)
    {
        auto start = std::search(_pending.begin(), _pending.end(),
                                 std::begin(SLP_SIGNATURE), std::end(SLP_SIGNATURE));
        // Keep a possible partial signature at the tail for the next fill.
        if (start == _pending.end())
        {
            size_t keep = std::min<size_t>(_pending.size(), sizeof(SLP_SIGNATURE) - 1);
            _pending.erase(_pending.begin(), _pending.end() - keep);
            RETURN_ERROR_AS_FALSE();
        }
        _pending.erase(_pending.begin(), start);

        if (_pending.size() < sizeof(SlpHeader))
            RETURN_ERROR_AS_FALSE();

        SlpHeader header;
        std::memcpy(&header, _pending.data(), sizeof(header));
        if (header.checksum != header_checksum(header))
        {
            _pending.erase(_pending.begin());
            continue;
        }

        size_t frame_length = sizeof(header) + header.data_length + SLP_CRC_LENGTH;
        if (_pending.size() < frame_length)
            RETURN_ERROR_AS_FALSE();

        u16be_t crc;
        std::memcpy(&crc, _pending.data() + frame_length - SLP_CRC_LENGTH, sizeof(crc));
        if (crc != slp_crc16(_pending.data(), frame_length - SLP_CRC_LENGTH))
        {
            _pending.erase(_pending.begin());
            continue;
        }

        out.dest_socket = header.dest_socket;
        out.src_socket = header.src_socket;
        out.type = static_cast<SlpType>(header.type);
        out.xid = header.xid;
        out.payload.assign(_pending.begin() + sizeof(header),
                           _pending.begin() + frame_length - SLP_CRC_LENGTH);
        _pending.erase(_pending.begin(), _pending.begin() + frame_length);
        RETURN_SUCCESS_AS_TRUE();
    }
}
