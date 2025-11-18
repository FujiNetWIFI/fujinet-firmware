#include "FujiBusPacket.h"

#include <cstring>   // std::memcpy
#include <cstddef>   // offsetof
#include <algorithm> // std::find

#include <iostream>
static void dbg_descr(const std::vector<uint8_t>& d)
{
    std::cerr << "DESCR:";
    for (auto b : d) std::cerr << " " << std::hex << int(b);
    std::cerr << std::dec << "\n";
}


// On-wire header layout (must stay exactly this size/layout)
struct fujibus_header {
    std::uint8_t device;   /* Destination Device */
    std::uint8_t command;  /* Command */
    std::uint16_t length;  /* Total length of packet including header */
    std::uint8_t checksum; /* Checksum of entire packet */
    std::uint8_t descr;    /* Describes the fields that follow (first descriptor) */
};

static_assert(sizeof(fujibus_header) == 6, "fujibus_header must be 6 bytes");
static_assert(offsetof(fujibus_header, checksum) == 4, "checksum offset mismatch");

#define FUJI_DESCR_COUNT_MASK    0x07
#define FUJI_DESCR_32_MASK       0x02
#define FUJI_DESCR_16_OR_32_MASK 0x04
#define FUJI_DESCR_ADDTL_MASK    0x80

static std::uint8_t fieldSizeTable[] = {0, 1, 1, 1, 1, 2, 2, 4};
static std::uint8_t numFieldsTable[]  = {0, 1, 2, 3, 4, 1, 2, 1};

namespace {

    // write `size` bytes of `value` (1,2,4) in little-endian
    inline void write_le(ByteBuffer& buf, std::uint32_t value, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            buf.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
    }
    
    // read `size` bytes in little-endian from `buf[offset..offset+size)`
    inline std::uint32_t read_le(const ByteBuffer& buf, std::size_t offset, std::size_t size)
    {
        std::uint32_t v = 0;
        for (std::size_t i = 0; i < size; ++i)
            v |= static_cast<std::uint32_t>(buf[offset + i]) << (8 * i);
        return v;
    }
    
    // helper to build descriptor value from field size + count, per FEP-004
    // table: 0..7 -> (count,size) combos :contentReference[oaicite:1]{index=1}
    //
    // 1: 1x u8
    // 2: 2x u8
    // 3: 3x u8
    // 4: 4x u8
    // 5: 1x u16
    // 6: 2x u16
    // 7: 1x u32
    inline std::uint8_t make_field_desc(unsigned fieldSize, unsigned fieldCount)
    {
        switch (fieldSize)
        {
            case 1:
                // 1..4 fields of uint8_t
                assert(fieldCount >= 1 && fieldCount <= 4);
                return static_cast<std::uint8_t>(fieldCount);
            case 2:
                // 1 or 2 fields of uint16_t
                assert(fieldCount == 1 || fieldCount == 2);
                return static_cast<std::uint8_t>(fieldCount == 1 ? 5 : 6);
            case 4:
                // only 1 uint32_t allowed (per current table)
                assert(fieldCount == 1);
                return 7;
            default:
                assert(false && "Invalid field size for descriptor");
                return 0;
        }
    }

    // Look up descriptor index (0..7) for given field size & count,
    // based on the spec tables fieldSizeTable/numFieldsTable.
    inline std::uint8_t lookup_field_desc(unsigned fieldSize, unsigned fieldCount)
    {
        for (int i = 0; i < 8; ++i)
        {
            if (fieldSizeTable[i] == fieldSize &&
                numFieldsTable[i] == fieldCount)
            {
                return static_cast<std::uint8_t>(i);
            }
        }
        assert(false && "No descriptor entry for given size/count");
        return 0;
    }

    // max how many fields we can encode in one descriptor, by element size
    inline unsigned max_fields_for_size(unsigned fieldSize)
    {
        switch (fieldSize)
        {
            case 1: return 4;
            case 2: return 2;
            case 4: return 1;
            default:
                assert(false && "Invalid field size");
                return 0;
        }
    }
    
    } // namespace
    

// ------------------ SLIP helpers ------------------

ByteBuffer FujiBusPacket::decodeSLIP(const ByteBuffer& input) const
{
    ByteBuffer output;
    output.reserve(input.size());  // worst case, same size

    const std::size_t len = input.size();
    std::size_t idx = 0;

    // Find the first SLIP_END
    while (idx < len && input[idx] != SLIP_END)
        ++idx;

    if (idx == len)
        return {}; // no frame start found

    // Decode from the byte after the first SLIP_END
    for (++idx; idx < len; ++idx)
    {
        std::uint8_t val = input[idx];
        if (val == SLIP_END)
            break;

        if (val == SLIP_ESCAPE)
        {
            if (++idx >= len)
                break; // truncated escape
            val = input[idx];
            if (val == SLIP_ESC_END)
                output.push_back(SLIP_END);
            else if (val == SLIP_ESC_ESC)
                output.push_back(SLIP_ESCAPE);
            // else: ignore malformed escape
        }
        else
        {
            output.push_back(val);
        }
    }

    return output;
}

ByteBuffer FujiBusPacket::encodeSLIP(const ByteBuffer& input) const
{
    ByteBuffer output;
    output.reserve(input.size() * 2 + 2);  // worst case, double size + 2 ENDs

    output.push_back(SLIP_END);
    for (std::uint8_t val : input)
    {
        if (val == SLIP_END || val == SLIP_ESCAPE)
        {
            output.push_back(SLIP_ESCAPE);
            if (val == SLIP_END)
                output.push_back(SLIP_ESC_END);
            else
                output.push_back(SLIP_ESC_ESC);
        }
        else
        {
            output.push_back(val);
        }
    }
    output.push_back(SLIP_END);

    return output;
}

// ------------------ Checksum ------------------

std::uint8_t FujiBusPacket::calcChecksum(const ByteBuffer& buf) const
{
    std::uint16_t chk = 0;

    for (std::size_t i = 0; i < buf.size(); ++i)
    {
        chk += buf[i];
        chk = (chk >> 8) + (chk & 0xFF); // fold carry
    }

    return static_cast<std::uint8_t>(chk);
}

// ------------------ Parsing ------------------

bool FujiBusPacket::parse(const ByteBuffer& input)
{
    ByteBuffer slipEncoded;

    // Find first SLIP_END, and treat the frame as starting there
    auto it = std::find(input.begin(), input.end(), static_cast<std::uint8_t>(SLIP_END));
    if (it != input.end())
        slipEncoded.assign(it, input.end());
    else
        slipEncoded = input;

    if (slipEncoded.size() < sizeof(fujibus_header) + 2)
        return false;
    if (slipEncoded.front() != SLIP_END || slipEncoded.back() != SLIP_END)
        return false;

    ByteBuffer decoded = decodeSLIP(slipEncoded);

    if (decoded.size() < sizeof(fujibus_header))
        return false;

    // Extract header from the front of decoded
    fujibus_header hdr{};
    std::memcpy(&hdr, decoded.data(), sizeof(hdr));

    if (hdr.length != decoded.size())
        return false;

    // Verify checksum:
    // - ck1 is the transmitted checksum
    // - ck2 is computed with the checksum byte zeroed
    const std::uint8_t ck1 = hdr.checksum;

    ByteBuffer tmp = decoded;
    tmp[offsetof(fujibus_header, checksum)] = 0;
    const std::uint8_t ck2 = calcChecksum(tmp);

    if (ck1 != ck2)
        return false;

    _device  = static_cast<fujiDeviceID_t>(hdr.device);
    _command = static_cast<fujiCommandID_t>(hdr.command);

    // ---- Descriptors & params ----

    std::size_t offset = sizeof(fujibus_header);
    std::vector<std::uint8_t> descrBytes;

    // First descriptor is in the header
    std::uint8_t d = hdr.descr;
    descrBytes.push_back(d);

    // Additional descriptors follow the header whenever bit 7 is set
    while (d & FUJI_DESCR_ADDTL_MASK)
    {
        if (offset >= decoded.size())
            return false; // malformed

        d = decoded[offset++];
        descrBytes.push_back(d);
    }

    // Now decode each descriptor into fields
    for (std::uint8_t dbyte : descrBytes)
    {
        unsigned fieldDesc  = dbyte & FUJI_DESCR_COUNT_MASK; // 0..7
        unsigned fieldCount = numFieldsTable[fieldDesc];
        if (!fieldCount)
            continue;

        unsigned fieldSize  = fieldSizeTable[fieldDesc];

        for (unsigned i = 0; i < fieldCount; ++i)
        {
            if (offset + fieldSize > decoded.size())
                return false;

            std::uint32_t v = read_le(decoded, offset, fieldSize);
            _params.emplace_back(v, static_cast<std::uint8_t>(fieldSize));
            offset += fieldSize;
        }
    }

    // Remaining bytes (if any) are payload
    if (offset < decoded.size())
    {
        _data.emplace(decoded.begin() + static_cast<std::ptrdiff_t>(offset),
                      decoded.end());
    }

    return true;
}

// ------------------ Serialization ------------------

ByteBuffer FujiBusPacket::serialize() const
{
    fujibus_header hdr{};
    hdr.device   = static_cast<std::uint8_t>(_device);
    hdr.command  = static_cast<std::uint8_t>(_command);
    hdr.length   = 0;      // fill later
    hdr.checksum = 0;
    hdr.descr    = 0;

    // We'll build:
    // [header][extra descr bytes][fields][payload]

    std::vector<std::uint8_t> descrBytes;
    ByteBuffer fieldsBuf;

    if (!_params.empty())
    {
        std::size_t i = 0;
        while (i < _params.size())
        {
            uint8_t size  = _params[i].size;
            unsigned maxN = max_fields_for_size(size);

            // how many contiguous same-size params can we pack into this descriptor?
            unsigned count = 1;
            while (i + count < _params.size() &&
                   _params[i + count].size == size &&
                   count < maxN)
            {
                ++count;
            }

            // descriptor index (0..7) from tables
            std::uint8_t descIndex = lookup_field_desc(size, count);
            descrBytes.push_back(descIndex);

            // write those params into fieldsBuf in little-endian
            for (unsigned k = 0; k < count; ++k)
            {
                const PacketParam& p = _params[i + k];
                write_le(fieldsBuf, p.value, size);
            }

            i += count;
        }

        // set "additional descriptor" flag (bit 7) on all but the last
        for (std::size_t idx = 0; idx + 1 < descrBytes.size(); ++idx)
            descrBytes[idx] |= FUJI_DESCR_ADDTL_MASK;
    }

    // First descriptor lives in hdr.descr
    if (!descrBytes.empty())
    {
        hdr.descr = descrBytes[0];
    }

    ByteBuffer output;
    output.reserve(sizeof(hdr) + descrBytes.size() + fieldsBuf.size() +
                   (_data ? _data->size() : 0));

    // placeholder for header
    output.resize(sizeof(hdr));

    // any additional descriptor bytes (after header)
    if (descrBytes.size() > 1)
    {
        output.insert(output.end(),
                      descrBytes.begin() + 1,
                      descrBytes.end());
    }

    // fields
    output.insert(output.end(), fieldsBuf.begin(), fieldsBuf.end());

    // payload (if any)
    if (_data && !_data->empty())
    {
        output.insert(output.end(), _data->begin(), _data->end());
    }

    // now we know the full length
    hdr.length = static_cast<std::uint16_t>(output.size());

    // write header with checksum=0 first
    std::memcpy(output.data(), &hdr, sizeof(hdr));

    // compute checksum over whole packet with checksum byte zeroed
    output[offsetof(fujibus_header, checksum)] = 0;
    hdr.checksum = calcChecksum(output);

    // write header again with correct checksum
    std::memcpy(output.data(), &hdr, sizeof(hdr));

    // finally, SLIP-encode
    return encodeSLIP(output);
}



// ------------------ Factory ------------------

std::unique_ptr<FujiBusPacket> FujiBusPacket::fromSerialized(const ByteBuffer& input)
{
    auto packet = std::make_unique<FujiBusPacket>();
    if (!packet->parse(input))
        return nullptr;
    return packet;
}
