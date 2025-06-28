#pragma once

#include <stdint.h>
#include <vector>

enum
{
	CMD_STATUS = 0,
	CMD_READ_BLOCK = 1,
	CMD_WRITE_BLOCK = 2,
	CMD_FORMAT = 3,
	CMD_CONTROL = 4,
	CMD_INIT = 5,
	CMD_OPEN = 6,
	CMD_CLOSE = 7,
	CMD_READ = 8,
	CMD_WRITE = 9
};

class Command
{
private:
    uint8_t request_sequence_number_ = 0;

public:
    explicit Command(const uint8_t request_sequence_number) : request_sequence_number_(request_sequence_number) {}
    virtual ~Command() = default;

    uint8_t get_request_sequence_number() const { return request_sequence_number_; }
    virtual std::vector<uint8_t> serialize() const = 0;
};
