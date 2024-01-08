#pragma once
#include "Response.h"

class CloseResponse : public Response
{
public:
	explicit CloseResponse(const uint8_t request_sequence_number, const uint8_t status);
	std::vector<uint8_t> serialize() const override;
};
