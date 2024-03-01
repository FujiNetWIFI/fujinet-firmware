#pragma once

#include <cstdint>
#include <vector>

#include "Command.h"

class Response : public Command
{
public:
	Response(uint8_t request_sequence_number, uint8_t status);
	std::vector<uint8_t> serialize() const override = 0;

	uint8_t get_status() const;

private:
	uint8_t status_ = 0;
};