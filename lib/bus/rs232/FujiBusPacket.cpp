#include "FujiBusPacket.h"

enum {
    SLIP_END     = 0xC0,
    SLIP_ESCAPE  = 0xDB,
    SLIP_ESC_END = 0xDC,
    SLIP_ESC_ESC = 0xDD,
};

typedef struct {
    uint8_t device;   /* Destination Device */
    uint8_t command;  /* Command */
    uint16_t length;  /* Total length of packet including header */
    uint8_t checksum; /* Checksum of entire packet */
    uint8_t fields;   /* Describes the fields that follow */
} fujibus_header;

#define FUJI_FIELD_COUNT_MASK    0x07
#define FUJI_FIELD_16_OR_32_MASK 0x40
#define FUJI_FIELD_32_MASK       0x20

static uint8_t fieldSizeTable[] = {0, 1, 1, 1, 1, 2, 2, 4};
static uint8_t numFieldsTable[] = {0, 1, 2, 3, 4, 1, 2, 1};

FujiBusPacket::FujiBusPacket(std::string_view input)
{
    if (!parse(input))
        throw std::invalid_argument("Invalid FujiBusPacket data");

    return;
}

#include <string>
#include <string_view>

std::string FujiBusPacket::decodeSLIP(std::string_view input)
{
    unsigned int idx;
    uint8_t val;
    std::string output;
    output.reserve(input.size());  // worst case, same size

    for (idx = 0; idx < input.size(); idx++)
    {
        if (input[idx] == SLIP_END)
            break;
    }

    for (idx = 0; idx < input.size(); idx++)
    {
        val = input[idx];
        if (val == SLIP_END)
            break;

        if (val == SLIP_ESCAPE)
        {
            idx++;
            val = input[idx];
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

uint8_t FujiBusPacket::calcChecksum(std::string_view buf)
{
    uint16_t idx, chk;

    for (idx = chk = 0; idx < buf.size(); idx++)
        chk = ((chk + buf[idx]) >> 8) + ((chk + buf[idx]) & 0xFF);
    return (uint8_t) chk;
}

bool FujiBusPacket::parse(std::string_view input)
{
    std::string decoded;
    fujibus_header *hdr;
    uint8_t ck1, ck2;
    unsigned int offset;
    unsigned int fieldCount;
    unsigned int idx, jdx;
    uint32_t val, bt;

    if (input.size() < sizeof(fujibus_header) + 2)
        return false;
    if (input[0] != SLIP_END || input.back() != SLIP_END)
        return false;

    decoded = decodeSLIP(input);
    hdr = (fujibus_header *) &decoded[0];
    if (hdr->length != decoded.size())
        return false;

    // Need to zero out checksum in order to calculate
    ck1 = hdr->checksum;
    hdr->checksum = 0;
    ck2 = calcChecksum(decoded);
    if (ck1 != ck2)
        return false;

    device = hdr->device;
    command = hdr->command;

    offset = sizeof(*hdr);
    fieldCount = numFieldsTable[hdr->fields & FUJI_FIELD_COUNT_MASK];
    if (fieldCount)
    {
        fieldSize = fieldSizeTable[fieldCount];
        for (idx = 0; idx < fieldCount; idx++)
        {
            for (val = jdx = 0; jdx < fieldSize; jdx++)
            {
                bt = decoded[offset + idx * fieldSize + jdx];
                val |= bt << (8 * jdx);
            }
            fields.push_back(val);
        }

        offset += idx * fieldSize;
    }

    if (offset < decoded.size())
        data = decoded.substr(offset);

    return true;
}

std::string FujiBusPacket::serialize()
{
    fujibus_header *hdr;
    std::string output;
    unsigned int idx, jdx;
    uint32_t val;

    output.resize(sizeof(*hdr));
    hdr = (fujibus_header *) output.data();
    hdr->device = device;
    hdr->command = command;
    hdr->checksum = 0;

    if (fields.size())
    {
        hdr->fields = fields.size() - 1;
        if (fieldSize > 1)
        {
            hdr->fields |= FUJI_FIELD_16_OR_32_MASK;
            if (fieldSize == 4)
                hdr->fields |= FUJI_FIELD_32_MASK;
            hdr->fields++;
        }

        for (idx = 0; idx < fields.size(); idx++)
        {
            for (jdx = 0, val = fields[idx]; jdx < fieldSize; jdx++, val >>= 8)
                output.push_back(val & 0xFF);
        }
    }

    if (data)
        output += *data;

    hdr->checksum = calcChecksum(output);
    return encodeSLIP(output);
}

std::unique_ptr<FujiBusPacket> FujiBusPacket::fromSerialized(std::string_view input)
{
    try {
        return std::make_unique<FujiBusPacket>(input);
    }
    catch (const std::invalid_argument&) {
        return nullptr;
    }
}
