#ifdef SP_OVER_SLIP

// ReSharper disable CppPassValueParameterByConstReference
#include <algorithm>

#include "ReadBlockResponse.h"

ReadBlockResponse::ReadBlockResponse(const uint8_t request_sequence_number, const uint8_t status) : Response(request_sequence_number, status), block_data_{} {}

std::vector<uint8_t> ReadBlockResponse::serialize() const
{
	std::vector<uint8_t> data;
	data.push_back(this->get_request_sequence_number());
	data.push_back(this->get_status());
	data.insert(data.end(), block_data_.begin(), block_data_.end());
	return data;
}

void ReadBlockResponse::set_block_data(std::vector<uint8_t>::const_iterator begin, std::vector<uint8_t>::const_iterator end)
{
	std::copy(begin, end, block_data_.begin()); // NOLINT(performance-unnecessary-value-param)
}

const std::array<uint8_t, 512>& ReadBlockResponse::get_block_data() const
{
	return block_data_;
}

#endif // SP_OVER_SLIP
