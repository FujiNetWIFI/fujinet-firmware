#pragma once

#include <stdint.h>

class SPoSLIP
{
private:
	uint8_t request_sequence_number_ = 0;

public:
	explicit SPoSLIP(const uint8_t request_sequence_number) : request_sequence_number_(request_sequence_number) {}
	virtual ~SPoSLIP() = default;

	uint8_t get_request_sequence_number() const { return request_sequence_number_; }
};
