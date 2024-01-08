#pragma once
#include "Response.h"

class ReadResponse : public Response
{
public:
	explicit ReadResponse(const uint8_t request_sequence_number, const uint8_t status);
	std::vector<uint8_t> serialize() const override;

	const std::vector<uint8_t>& get_data() const { return data_; }
	void set_data(const std::vector<uint8_t>::const_iterator& begin, const std::vector<uint8_t>::const_iterator& end);

private:
	std::vector<uint8_t> data_;
};
