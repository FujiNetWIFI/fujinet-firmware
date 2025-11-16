#include "FujiBusPacket.h"

#include "../../include/debug.h"
#include "utils.h"

typedef struct {
    uint8_t device;   /* Destination Device */
    uint8_t command;  /* Command */
    uint16_t length;  /* Total length of packet including header */
    uint8_t checksum; /* Checksum of entire packet */
    uint8_t descr;   /* Describes the fields that follow */
} fujibus_header;

#define FUJI_DESCR_COUNT_MASK    0x07
#define FUJI_DESCR_32_MASK       0x02
#define FUJI_DESCR_16_OR_32_MASK 0x04
#define FUJI_DESCR_ADDTL_MASK    0x80

static uint8_t fieldSizeTable[] = {0, 1, 1, 1, 1, 2, 2, 4};
static uint8_t numFieldsTable[] = {0, 1, 2, 3, 4, 1, 2, 1};

std::string FujiBusPacket::decodeSLIP(std::string_view input)
{
    unsigned int idx;
    uint8_t val;
    std::string output;
    output.reserve(input.size());  // worst case, same size

    const uint8_t *ptr = (uint8_t *) input.data();
    for (idx = 0; idx < input.size(); idx++)
    {
        if (ptr[idx] == SLIP_END)
            break;
    }

    for (idx++; idx < input.size(); idx++)
    {
        val = ptr[idx];
        if (val == SLIP_END)
            break;

        if (val == SLIP_ESCAPE)
        {
            idx++;
            val = ptr[idx];
            if (val == SLIP_ESC_END)
                output.push_back(SLIP_END);
            else if (val == SLIP_ESC_ESC)
                output.push_back(SLIP_ESCAPE);
        }
        else
            output.push_back(val);
    }

    return output;
}

std::string FujiBusPacket::encodeSLIP(std::string_view input)
{
    unsigned int idx;
    uint8_t val;
    std::string output;
    output.reserve(input.size() * 2);  // worst case, double size

    output.push_back(SLIP_END);
    for (idx = 0; idx < input.size(); idx++)
    {
        val = input[idx];
        if (val == SLIP_END || val == SLIP_ESCAPE)
        {
            output.push_back(SLIP_ESCAPE);
            if (val == SLIP_END)
                output.push_back(SLIP_ESC_END);
            else
                output.push_back(SLIP_ESC_ESC);
        }
        else
            output.push_back(val);
    }
    output.push_back(SLIP_END);

    return output;
}

uint8_t FujiBusPacket::calcChecksum(const std::string &buf)
{
    uint16_t idx, chk;
    uint8_t *ptr = (uint8_t *) buf.data();

    for (idx = chk = 0; idx < buf.size(); idx++)
        chk = ((chk + ptr[idx]) >> 8) + ((chk + ptr[idx]) & 0xFF);
    return (uint8_t) chk;
}

bool FujiBusPacket::parse(std::string_view input)
{
    std::string decoded;
    fujibus_header *hdr;
    std::string_view slipEncoded = input;

    Debug_printv("Incoming:\n%s\n", util_hexdump(input.data(), input.size()).c_str());

    size_t slipMarker = input.find(SLIP_END);
    if (slipMarker != std::string::npos)
        slipEncoded = std::string_view(input).substr(slipMarker);

    if (slipEncoded.size() < sizeof(fujibus_header) + 2)
        return false;
    if (((uint8_t) slipEncoded[0]) != SLIP_END || ((uint8_t) slipEncoded.back()) != SLIP_END)
        return false;

    decoded = decodeSLIP(slipEncoded);
    Debug_printv("Decoded:\n%s\n", util_hexdump(decoded.data(), decoded.size()).c_str());

    if (decoded.size() < sizeof(fujibus_header))
        return false;
    hdr = (fujibus_header *) &decoded[0];
    Debug_printv("Header: dev:%02x cmd:%02x len:%d chk:%02x fld:%02x",
                 hdr->device, hdr->command, hdr->length, hdr->checksum, hdr->descr);

    if (hdr->length != decoded.size())
        return false;

    {
        uint8_t ck1, ck2;

        // Need to zero out checksum in order to calculate
        ck1 = hdr->checksum;
        hdr->checksum = 0;
        ck2 = calcChecksum(decoded);
        if (ck1 != ck2)
            return false;
    }

    _device = static_cast<fujiDeviceID_t>(hdr->device);
    _command = static_cast<fujiCommandID_t>(hdr->command);

    {
        unsigned val, offset;
        std::vector<uint8_t> descr;

        offset = sizeof(*hdr) - 1;
        do {
            val = decoded[offset++];
            descr.push_back(val & FUJI_DESCR_COUNT_MASK);
        } while (val & FUJI_DESCR_ADDTL_MASK);

        for (const auto &fieldDesc : descr)
        {
            unsigned fieldSize, fieldCount;
            unsigned idx, jdx;

            fieldCount = numFieldsTable[fieldDesc];
            if (fieldCount)
            {
                fieldSize = fieldSizeTable[fieldDesc];

                for (idx = 0; idx < fieldCount; idx++)
                {
                    uint32_t val, bt;

                    for (val = jdx = 0; jdx < fieldSize; jdx++)
                    {
                        bt = (uint8_t) decoded[offset + idx * fieldSize + jdx];
                        val |= bt << (8 * jdx);
                    }
                    _params.emplace_back(val, fieldSize);
                }

                offset += idx * fieldSize;
            }
        }

        if (offset < decoded.size())
            _data = decoded.substr(offset);
    }

    return true;
}

std::string FujiBusPacket::serialize()
{
    fujibus_header hdr, *hptr;

    hdr.device = _device;
    hdr.command = _command;
    hdr.length = sizeof(hdr);
    hdr.checksum = 0;
    hdr.descr = 0;

    std::string output(sizeof(hdr), '\0');

    if (_params.size())
    {
        std::vector<uint8_t> descr;
        unsigned fieldSize = 0;
        unsigned idx, jdx, kdx, val;
        PacketParam *param;


        for (idx = 0; idx < _params.size(); idx++)
        {
            for (jdx = idx; jdx < _params.size(); jdx++)
            {
                param = &_params[jdx];
                if (fieldSize && fieldSize != param->size)
                    break;
                fieldSize = param->size;

                for (kdx = val = 0; kdx < fieldSize; kdx++, val >>= 8)
                    output.push_back(val & 0xFF);
            }

            uint8_t fieldDescr = jdx - idx - 1;
            if (fieldSize > 1)
            {
                fieldDescr |= FUJI_DESCR_16_OR_32_MASK;
                if (fieldSize == 4)
                    fieldDescr |= FUJI_DESCR_32_MASK;
                fieldDescr++;
            }

            descr.push_back(fieldDescr);
        }

        hdr.descr = descr[0];
        descr.erase(descr.begin());
        output.insert(0, (char *) descr.data(), descr.size());
    }

    if (_data)
        output += *_data;

    hdr.length = output.size();
    hptr = (fujibus_header *) output.data();
    *hptr = hdr;
    hptr->checksum = calcChecksum(output);
    Debug_printv("Packet header: dev:%02x cmd:%02x len:%d chk:%02x fld:%02x",
                 hptr->device, hptr->command, hptr->length, hptr->checksum, hptr->descr);
    Debug_printv("\n%s\n", util_hexdump(output.data(), output.size()).c_str());
    auto encoded = encodeSLIP(output);
    Debug_printv("Encoded:\n%s\n", util_hexdump(encoded.data(), encoded.size()).c_str());
    return encoded;
}

std::unique_ptr<FujiBusPacket> FujiBusPacket::fromSerialized(std::string_view input)
{
    auto packet = std::make_unique<FujiBusPacket>();
    if (!packet->parse(input))
        return nullptr;
    return packet;
}
