#include "FujiBusPacket.h"

#include <cstring>   // std::memcpy
#include <cstddef>   // offsetof
#include <algorithm> // std::find

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

    // Parse descriptors and field values
    {
        std::size_t offset = sizeof(fujibus_header) - 1; // descr is last byte of header
        std::vector<std::uint8_t> descrBytes;

        // Read descriptor chain: each byte may indicate there is an additional descriptor
        std::uint8_t val;
        do {
            if (offset >= decoded.size())
                return false; // malformed
            val = decoded[offset++];
            descrBytes.push_back(val);
        } while (val & FUJI_DESCR_ADDTL_MASK);

        // For each descriptor, use the low 3 bits to index the tables
        for (std::uint8_t descrByte : descrBytes)
        {
            const unsigned fieldDesc  = descrByte & FUJI_DESCR_COUNT_MASK;
            const unsigned fieldCount = numFieldsTable[fieldDesc];

            if (fieldCount == 0)
                continue;

            const unsigned fieldSize = fieldSizeTable[fieldDesc];
            const std::size_t blockSize = static_cast<std::size_t>(fieldCount) * fieldSize;

            if (offset + blockSize > decoded.size())
                return false; // truncated

            for (unsigned idx = 0; idx < fieldCount; ++idx)
            {
                std::uint32_t v = 0;
                for (unsigned jdx = 0; jdx < fieldSize; ++jdx)
                {
                    std::uint32_t bt = decoded[offset + idx * fieldSize + jdx];
                    v |= bt << (8 * jdx); // little-endian
                }
                _params.emplace_back(v, static_cast<std::uint8_t>(fieldSize));
            }

            offset += blockSize;
        }

        // Remaining bytes (if any) are payload
        if (offset < decoded.size())
        {
            _data.emplace(decoded.begin() + static_cast<std::ptrdiff_t>(offset),
                          decoded.end());
        }
    }

    return true;
}

// ------------------ Serialization ------------------

ByteBuffer FujiBusPacket::serialize() const
{
    fujibus_header hdr{};
    hdr.device   = static_cast<std::uint8_t>(_device);
    hdr.command  = static_cast<std::uint8_t>(_command);
    hdr.length   = sizeof(hdr); // will be updated later
    hdr.checksum = 0;
    hdr.descr    = 0;

    // Start with placeholder space for header
    ByteBuffer output(sizeof(hdr), 0);

    if (!_params.empty())
    {
        std::vector<std::uint8_t> descr;
        unsigned fieldSize = 0;
        unsigned idx, jdx, kdx;
        PacketParam* param;

        // NOTE: This block is a near-direct translation of your existing logic,
        // just using ByteBuffer instead of std::string. It may deserve a
        // separate clean-up / re-think, but is kept logically equivalent.
        for (idx = 0; idx < _params.size(); idx++)
        {
            for (jdx = idx; jdx < _params.size(); jdx++)
            {
                param = const_cast<PacketParam*>(&_params[jdx]); // same as before, but we only read

                if (fieldSize && fieldSize != param->size)
                    break;
                fieldSize = param->size;

                // WARNING: This currently pushes zeros (original code also
                // did not use param->value here). Left as-is to preserve
                // behaviour; you likely want to revisit and actually
                // serialize param->value.
                std::uint32_t val = 0;
                for (kdx = 0; kdx < fieldSize; kdx++, val >>= 8)
                    output.push_back(static_cast<std::uint8_t>(val & 0xFF));
            }

            std::uint8_t fieldDescr = static_cast<std::uint8_t>(jdx - idx - 1);
            if (fieldSize > 1)
            {
                fieldDescr |= FUJI_DESCR_16_OR_32_MASK;
                if (fieldSize == 4)
                    fieldDescr |= FUJI_DESCR_32_MASK;
                fieldDescr++;
            }

            descr.push_back(fieldDescr);
        }

        // First descriptor lives in hdr.descr, remaining descriptors are
        // prepended to the buffer as per your original code.
        hdr.descr = descr[0];
        descr.erase(descr.begin());

        if (!descr.empty())
        {
            output.insert(output.begin(),
                          descr.begin(),
                          descr.end());
        }
    }

    // Append payload if present
    if (_data && !_data->empty())
    {
        output.insert(output.end(),
                      _data->begin(),
                      _data->end());
    }

    // Now fix up header length and checksum
    hdr.length = static_cast<std::uint16_t>(output.size());

    // Write header into the first bytes of output
    std::memcpy(output.data(), &hdr, sizeof(hdr));

    // Zero checksum in-place, compute, then store
    output[offsetof(fujibus_header, checksum)] = 0;
    hdr.checksum = calcChecksum(output);
    std::memcpy(output.data(), &hdr, sizeof(hdr));

    // Finally SLIP-encode the whole packet
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
