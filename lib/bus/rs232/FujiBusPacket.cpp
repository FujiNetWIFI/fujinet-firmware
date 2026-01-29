#include "FujiBusPacket.h"

#include <cstring>   // std::memcpy
#include <cstddef>   // offsetof
#include <algorithm> // std::find
#include <new>

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

#define FUJI_DESCR_COUNT_MASK  0x07
#define FUJI_DESCR_EXCEEDS_U8  0x04
#define FUJI_DESCR_EXCEEDS_U16 0x02
#define FUJI_DESCR_ADDTL_MASK  0x80
#define MAX_BYTES_PER_DESCR 4

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
} // namespace

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

    // Avoids a compiler warning with GCC 15, which did not like the initial push_back of SLIP_END on a size 0 buffer (even though reserve has it with enough space).
    output.resize(1);
    output[0] = SLIP_END;

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
    fujibus_header *hdr;
    hdr = (fujibus_header *) &decoded[0];

    if (hdr->length != decoded.size())
        return false;

    // Verify checksum:
    // - ck1 is the transmitted checksum
    // - ck2 is computed with the checksum byte zeroed
    const std::uint8_t ck1 = hdr->checksum;

    hdr->checksum = 0;
    const std::uint8_t ck2 = calcChecksum(decoded);

    if (ck1 != ck2)
        return false;

    _device  = static_cast<fujiDeviceID_t>(hdr->device);
    _command = static_cast<fujiCommandID_t>(hdr->command);

    // ---- Descriptors & params ----

    std::size_t offset = sizeof(fujibus_header);
    ByteBuffer descrBytes;

    // First descriptor is in the header
    std::uint8_t dsc = hdr->descr;
    descrBytes.push_back(dsc);

    // Additional descriptors follow the header whenever bit 7 is set
    while (dsc & FUJI_DESCR_ADDTL_MASK)
    {
        if (offset >= decoded.size())
            return false; // malformed

        dsc = decoded[offset++];
        descrBytes.push_back(dsc);
    }

    // Now decode each descriptor into fields
    for (std::uint8_t dbyte : descrBytes)
    {
        unsigned fieldDesc  = dbyte & FUJI_DESCR_COUNT_MASK; // 0..7
        unsigned fieldCount = numFieldsTable[fieldDesc];
        if (!fieldCount)
            continue;

        unsigned fieldSize  = fieldSizeTable[fieldDesc];

        for (unsigned idx = 0; idx < fieldCount; ++idx)
        {
            if (offset + fieldSize > decoded.size())
                return false;

            std::uint32_t val = read_le(decoded, offset, fieldSize);
            _params.emplace_back(val, static_cast<std::uint8_t>(fieldSize));
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

ByteBuffer FujiBusPacket::serialize() const
{
    fujibus_header hdr, *hptr;

    hdr.device = _device;
    hdr.command = _command;
    hdr.length = sizeof(hdr);
    hdr.checksum = 0;
    hdr.descr = 0;

    ByteBuffer output(sizeof(hdr), '\0');

    if (!_params.empty())
    {
        ByteBuffer descr;
        unsigned fieldSize, bytesWritten;
        unsigned idx, count, val, lenParams;
        const PacketParam *param;


        for (idx = 0, lenParams = _params.size(); idx < lenParams; idx += count)
        {
            for (count = fieldSize = bytesWritten = 0; count + idx < lenParams; count++)
            {
                param = &_params[count + idx];
                if ((fieldSize && fieldSize != param->size) || bytesWritten == MAX_BYTES_PER_DESCR)
                    break;
                fieldSize = param->size;

                write_le(output, param->value, param->size);
                bytesWritten += param->size;
            }

            uint8_t fieldDescr = count;
            if (fieldSize > 1)
            {
                fieldDescr += FUJI_DESCR_EXCEEDS_U8;
                if (fieldSize > 2)
                    fieldDescr += FUJI_DESCR_EXCEEDS_U16;
            }

            descr.push_back(fieldDescr | FUJI_DESCR_ADDTL_MASK);
        }

        descr.back() -= FUJI_DESCR_ADDTL_MASK;
        hdr.descr = descr[0];
        descr.erase(descr.begin());
        output.insert(output.begin() + sizeof(hdr), descr.begin(), descr.end());
    }

    if (_data)
        output.insert(output.end(), _data->begin(), _data->end());

    hdr.length = output.size();
    hptr = (fujibus_header *) output.data();
    *hptr = hdr;
    hptr->checksum = calcChecksum(output);
    auto encoded = encodeSLIP(output);
    return encoded;
}

// ------------------ Factory ------------------
std::unique_ptr<FujiBusPacket> FujiBusPacket::fromSerialized(const ByteBuffer& input)
{
    auto packet = std::make_unique<FujiBusPacket>();
    if (!packet->parse(input))
        return nullptr;
    return packet;
}

#ifndef FUJIBUS_TESTING
#include "debug.h"
#define MAGIC_ENUM_RANGE_MIN -128
#define MAGIC_ENUM_RANGE_MAX 256
#include "magic_enum.hpp"

template <typename E>
std::string enum_or_hex(E value) {
    auto label = magic_enum::enum_name(value);
    if (!label.empty()) return std::string(label);

    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%02X", static_cast<unsigned>(value));
    return buf;
}

void FujiBusPacket::debugPrint()
{
    auto dev_str = enum_or_hex(_device);
    auto cmd_str = enum_or_hex(_command);

    Debug_printf("Device: %s\n", dev_str.c_str());
    Debug_printf("Command: %s\n", cmd_str.c_str());
    Debug_printf("Param count: %d\n", _params.size());
    if (_params.size())
    {
        for (PacketParam parm : _params)
            Debug_printf("  U%d: 0x%0*x %u", parm.size * 8,
                         parm.size * 2, parm.value, parm.value);
        Debug_printf("\n");
    }

    return;
}
#endif /* ! FUJIBUS_TESTING */
