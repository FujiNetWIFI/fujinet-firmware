#pragma once

#include <vector>
#include <stdint.h>

enum
{
	SP_STATUS = 0,
	SP_READ_BLOCK = 1,
	SP_WRITE_BLOCK = 2,
	SP_FORMAT = 3,
	SP_CONTROL = 4,
	SP_INIT = 5,
	SP_OPEN = 6,
	SP_CLOSE = 7,
	SP_READ = 8,
	SP_WRITE = 9,
	SP_RESET = 10
};

class SmartPortPacket
{
private:
    uint8_t request_sequence_number_ = 0;

public:
    explicit SmartPortPacket(const uint8_t request_sequence_number) : request_sequence_number_(request_sequence_number) {}
    virtual ~SmartPortPacket() = default;

    uint8_t get_request_sequence_number() const { return request_sequence_number_; }
    virtual std::vector<uint8_t> serialize() const = 0;
};