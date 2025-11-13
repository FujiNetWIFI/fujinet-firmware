#include "FujiBusPacket.h"

#include "../../include/debug.h"
#include "utils.h"

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

std::string FujiBusPacket::decodeSLIP(const std::string &input)
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

std::string FujiBusPacket::encodeSLIP(const std::string &input)
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

uint8_t FujiBusPacket::calcChecksum(const std::string &input)
{
    uint16_t idx, chk;
    uint8_t *buf = (uint8_t *) input.data();

    for (idx = chk = 0; idx < input.size(); idx++)
        chk = ((chk + buf[idx]) >> 8) + ((chk + buf[idx]) & 0xFF);
    return (uint8_t) chk;
}

bool FujiBusPacket::parse(const std::string &input)
{
    std::string decoded;
    fujibus_header *hdr;
    uint8_t ck1, ck2;
    unsigned int offset;
    unsigned int fieldCount;
    unsigned int idx, jdx;
    uint32_t val, bt;

    Debug_printv("Incoming:\n%s\n", util_hexdump(input.data(), input.size()).c_str());

    if (input.size() < sizeof(fujibus_header) + 2)
        return false;
    if (((uint8_t) input[0]) != SLIP_END || ((uint8_t) input.back()) != SLIP_END)
        return false;

    decoded = decodeSLIP(input);
    Debug_printv("Decoded:\n%s\n", util_hexdump(decoded.data(), decoded.size()).c_str());

    if (decoded.size() < sizeof(fujibus_header))
        return false;
    hdr = (fujibus_header *) &decoded[0];
    Debug_printv("Header: dev:%02x cmd:%02x len:%d chk:%02x fld:%02x",
                 hdr->device, hdr->command, hdr->length, hdr->checksum, hdr->fields);

    if (hdr->length != decoded.size())
        return false;

    // Need to zero out checksum in order to calculate
    ck1 = hdr->checksum;
    hdr->checksum = 0;
    ck2 = calcChecksum(decoded);
    if (ck1 != ck2)
        return false;

    _device = static_cast<fujiDeviceID_t>(hdr->device);
    _command = static_cast<fujiCommandID_t>(hdr->command);

    offset = sizeof(*hdr);
    fieldCount = numFieldsTable[hdr->fields & FUJI_FIELD_COUNT_MASK];
    if (fieldCount)
    {
        _fieldSize = fieldSizeTable[hdr->fields & FUJI_FIELD_COUNT_MASK];

        for (idx = 0; idx < fieldCount; idx++)
        {
            for (val = jdx = 0; jdx < _fieldSize; jdx++)
            {
                bt = (uint8_t) decoded[offset + idx * _fieldSize + jdx];
                val |= bt << (8 * jdx);
            }
            _params.push_back(val);
        }

        offset += idx * _fieldSize;
    }

    if (offset < decoded.size())
        _data = decoded.substr(offset);

    return true;
}

std::string FujiBusPacket::serialize()
{
    fujibus_header hdr, *hptr;
    unsigned int idx, jdx;
    uint32_t val;

    hdr.device = _device;
    hdr.command = _command;
    hdr.length = sizeof(hdr);
    hdr.checksum = 0;
    hdr.fields = 0;

    std::string output(sizeof(hdr), '\0');

    if (_params.size())
    {
        hdr.fields = _params.size() - 1;
        if (_fieldSize > 1)
        {
            hdr.fields |= FUJI_FIELD_16_OR_32_MASK;
            if (_fieldSize == 4)
                hdr.fields |= FUJI_FIELD_32_MASK;
            hdr.fields++;
        }

        for (idx = 0; idx < _params.size(); idx++)
        {
            for (jdx = 0, val = _params[idx]; jdx < _fieldSize; jdx++, val >>= 8)
                output.push_back(val & 0xFF);
        }
    }

    if (_data)
        output += *_data;

    hdr.length = output.size();
    hptr = (fujibus_header *) output.data();
    *hptr = hdr;
    hptr->checksum = calcChecksum(output);
    Debug_printv("Packet header: dev:%02x cmd:%02x len:%d chk:%02x fld:%02x",
                 hptr->device, hptr->command, hptr->length, hptr->checksum, hptr->fields);
    return encodeSLIP(output);
}

std::unique_ptr<FujiBusPacket> FujiBusPacket::fromSerialized(const std::string &input)
{
    auto packet = std::make_unique<FujiBusPacket>();
    if (!packet->parse(input))
        return nullptr;
    return packet;
}
