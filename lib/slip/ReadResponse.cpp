#ifdef SP_OVER_SLIP

// ReSharper disable CppPassValueParameterByConstReference
#include "ReadResponse.h"

ReadResponse::ReadResponse(const uint8_t request_sequence_number, const uint8_t status) : Response(request_sequence_number, status) {}

std::vector<uint8_t> ReadResponse::serialize() const
{
	std::vector<uint8_t> data;
	data.push_back(this->get_request_sequence_number());
	data.push_back(this->get_status());
	data.insert(data.end(), get_data().begin(), get_data().end());
	return data;
}

void ReadResponse::set_data(const std::vector<uint8_t>::const_iterator& begin, const std::vector<uint8_t>::const_iterator& end)
{
	const size_t new_size = std::distance(begin, end);
	data_.resize(new_size);
	std::copy(begin, end, data_.begin()); // NOLINT(performance-unnecessary-value-param)
}

#endif // SP_OVER_SLIP
