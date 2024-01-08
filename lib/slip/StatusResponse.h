#pragma once

#include <vector>
#include <stdint.h>
#include "Response.h"

class StatusResponse : public Response
{
public:
	explicit StatusResponse(uint8_t request_sequence_number, uint8_t status);
	std::vector<uint8_t> serialize() const override;

	const std::vector<uint8_t>& get_data() const;
	void add_data(uint8_t d);
	void set_data(const std::vector<uint8_t>::const_iterator& begin, const std::vector<uint8_t>::const_iterator& end);

private:
	std::vector<uint8_t> data_;

};
