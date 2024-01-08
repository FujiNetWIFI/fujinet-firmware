#pragma once
#include "Response.h"

class ResetResponse : public Response
{
public:
	explicit ResetResponse(const uint8_t request_sequence_number, const uint8_t status);
	std::vector<uint8_t> serialize() const override;
};
