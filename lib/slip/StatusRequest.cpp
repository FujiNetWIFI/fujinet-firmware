#ifdef SP_OVER_SLIP

#include <stdexcept>

#include "SmartPortCodes.h"
#include "StatusRequest.h"
#include "StatusResponse.h"

StatusRequest::StatusRequest(const uint8_t request_sequence_number, const uint8_t sp_unit, const uint8_t status_code)
	: Request(request_sequence_number, SP_STATUS, sp_unit), status_code_(status_code) {}

std::vector<uint8_t> StatusRequest::serialize() const
{
	std::vector<uint8_t> request_data;
	request_data.push_back(this->get_request_sequence_number());
	request_data.push_back(this->get_command_number());
	request_data.push_back(this->get_sp_unit());
	request_data.push_back(this->get_status_code());

	return request_data;
}

std::unique_ptr<Response> StatusRequest::deserialize(const std::vector<uint8_t>& data) const
{
	if (data.size() < 2)
	{
		throw std::runtime_error("Not enough data to deserialize StatusResponse");
	}

	auto response = std::make_unique<StatusResponse>(data[0], data[1]);

	if (response->get_status() == 0 && data.size() > 2)
	{
		for (size_t i = 2; i < data.size(); ++i)
		{
			response->add_data(data[i]);
		}
	}

	return response;
}

void StatusRequest::create_command(uint8_t* cmd_data) const
{
	init_command(cmd_data);
	cmd_data[4] = status_code_;
}

std::unique_ptr<Response> StatusRequest::create_response(uint8_t source, uint8_t status, const uint8_t* data, uint16_t num) const
{
    std::unique_ptr<StatusResponse> response = std::make_unique<StatusResponse>(get_request_sequence_number(), status);
			// Copy the return data if the status is OK
		if (status == 0) {
			std::vector<uint8_t> data_vector(data, data + num);
			response->set_data(data_vector.begin(), data_vector.end());
		}

    return response;
}

#endif // SP_OVER_SLIP
