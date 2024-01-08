#ifdef SP_OVER_SLIP

#include "StatusResponse.h"
#include "StatusRequest.h"

StatusResponse::StatusResponse(const uint8_t request_sequence_number, const uint8_t status) : Response(request_sequence_number, status) {}

const std::vector<uint8_t>& StatusResponse::get_data() const { return data_; }

void StatusResponse::add_data(const uint8_t d) { data_.push_back(d); }

void StatusResponse::set_data(const std::vector<uint8_t>::const_iterator& begin, const std::vector<uint8_t>::const_iterator& end)
{
	const size_t new_size = std::distance(begin, end);
	data_.resize(new_size);
	std::copy(begin, end, data_.begin()); // NOLINT(performance-unnecessary-value-param)
}

std::vector<uint8_t> StatusResponse::serialize() const
{
	std::vector<uint8_t> data;
	data.push_back(this->get_request_sequence_number());
	data.push_back(this->get_status());

	for (uint8_t b : get_data())
	{
		data.push_back(b);
	}
	return data;
}

#endif // SP_OVER_SLIP
