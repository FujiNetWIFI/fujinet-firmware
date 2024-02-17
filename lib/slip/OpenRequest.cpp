#ifdef SP_OVER_SLIP

#include "OpenRequest.h"
#include "OpenResponse.h"

OpenRequest::OpenRequest(const uint8_t request_sequence_number, const uint8_t sp_unit)
	: Request(request_sequence_number, SP_OPEN, sp_unit) {}

std::vector<uint8_t> OpenRequest::serialize() const
{
	std::vector<uint8_t> request_data;
	request_data.push_back(this->get_request_sequence_number());
	request_data.push_back(this->get_command_number());
	request_data.push_back(this->get_sp_unit());
	return request_data;
}

std::unique_ptr<Response> OpenRequest::deserialize(const std::vector<uint8_t>& data) const
{
	if (data.size() < 2)
	{
		throw std::runtime_error("Not enough data to deserialize OpenResponse");
	}

	auto response = std::make_unique<OpenResponse>(data[0], data[1]);
	return response;
}

void OpenRequest::create_command(uint8_t* cmd_data) const
{
	init_command(cmd_data);
}

std::unique_ptr<Response> OpenRequest::create_response(uint8_t source, uint8_t status, const uint8_t* data, uint16_t num) const
{
    std::unique_ptr<OpenResponse> response = std::make_unique<OpenResponse>(get_request_sequence_number(), status);
    return response;
}

#endif // SP_OVER_SLIP
