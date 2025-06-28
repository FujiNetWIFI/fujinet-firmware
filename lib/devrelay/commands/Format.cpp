#ifdef DEV_RELAY_SLIP

#include "Format.h"

FormatRequest::FormatRequest(const uint8_t request_sequence_number, const uint8_t param_count, const uint8_t device_id) : Request(request_sequence_number, CMD_FORMAT, param_count, device_id) {}

std::vector<uint8_t> FormatRequest::serialize() const
{
	std::vector<uint8_t> request_data;
	request_data.push_back(this->get_request_sequence_number());
	request_data.push_back(this->get_command_number());
	request_data.push_back(this->get_param_count());
	request_data.push_back(this->get_device_id());
	request_data.resize(11);
	return request_data;
}

std::unique_ptr<Response> FormatRequest::deserialize(const std::vector<uint8_t> &data) const
{
	if (data.size() < 2)
	{
		throw std::runtime_error("Not enough data to deserialize FormatResponse");
	}

	auto response = std::make_unique<FormatResponse>(data[0], data[1]);
	return response;
}

FormatResponse::FormatResponse(const uint8_t request_sequence_number, const uint8_t status) : Response(request_sequence_number, status) {}

std::vector<uint8_t> FormatResponse::serialize() const
{
	std::vector<uint8_t> data;
	data.push_back(this->get_request_sequence_number());
	data.push_back(this->get_status());
	return data;
}

void FormatRequest::create_command(uint8_t* cmd_data) const
{
	init_command(cmd_data);
}

std::unique_ptr<Response> FormatRequest::create_response(uint8_t source, uint8_t status, const uint8_t* data, uint16_t num) const
{
    std::unique_ptr<FormatResponse> response = std::make_unique<FormatResponse>(get_request_sequence_number(), status);
    return response;
}

#endif
