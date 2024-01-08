#pragma once

#include <vector>
#include <cstdint>
#include "Packet.h"

class Response : public Packet
{
public:
	Response(uint8_t request_sequence_number, uint8_t status);
	std::vector<uint8_t> serialize() const override = 0;

	uint8_t get_status() const;

private:
	uint8_t status_ = 0;
};