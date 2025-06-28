#ifdef DEV_RELAY_SLIP

#include "Close.h"

CloseRequest::CloseRequest(const uint8_t request_sequence_number, const uint8_t param_count, const uint8_t device_id) : Request(request_sequence_number, CMD_CLOSE, param_count, device_id) {}

std::vector<uint8_t> CloseRequest::serialize() const
{
	std::vector<uint8_t> request_data;
	request_data.push_back(this->get_request_sequence_number());
	request_data.push_back(this->get_command_number());
	request_data.push_back(this->get_param_count());
	request_data.push_back(this->get_device_id());
	request_data.resize(11);
	return request_data;
}

std::unique_ptr<Response> CloseRequest::deserialize(const std::vector<uint8_t> &data) const
{
	if (data.size() < 2)
	{
		throw std::runtime_error("Not enough data to deserialize CloseResponse");
	}

	auto response = std::make_unique<CloseResponse>(data[0], data[1]);
	return response;
}

void CloseRequest::create_command(uint8_t* cmd_data) const
{
	init_command(cmd_data);
}

std::unique_ptr<Response> CloseRequest::create_response(uint8_t source, uint8_t status, const uint8_t* data, uint16_t num) const
{
	std::unique_ptr<CloseResponse> response = std::make_unique<CloseResponse>(get_request_sequence_number(), status);
	return response;
}


CloseResponse::CloseResponse(const uint8_t request_sequence_number, const uint8_t status) : Response(request_sequence_number, status) {}

std::vector<uint8_t> CloseResponse::serialize() const
{
	std::vector<uint8_t> data;
	data.push_back(this->get_request_sequence_number());
	data.push_back(this->get_status());
	return data;
}


#endif
