#ifdef SP_OVER_SLIP

#include <algorithm>
#include "ControlRequest.h"

#include "ControlResponse.h"
#include "SmartPortCodes.h"


ControlRequest::ControlRequest(const uint8_t request_sequence_number, const uint8_t sp_unit, const uint8_t control_code, std::vector<uint8_t>& data)
	: Request(request_sequence_number, SP_CONTROL, sp_unit), control_code_(control_code), data_(std::move(data)) {}

std::vector<uint8_t> ControlRequest::serialize() const
{
	std::vector<uint8_t> request_data;
	request_data.push_back(this->get_request_sequence_number());
	request_data.push_back(this->get_command_number());
	request_data.push_back(this->get_sp_unit());
	request_data.push_back(this->get_control_code());
	request_data.insert(request_data.end(), get_data().begin(), get_data().end());
	return request_data;
}

std::unique_ptr<Response> ControlRequest::deserialize(const std::vector<uint8_t>& data) const
{
	if (data.size() < 2)
	{
		throw std::runtime_error("Not enough data to deserialize ControlResponse");
	}

	auto response = std::make_unique<ControlResponse>(data[0], data[1]);
	return response;
}

void ControlRequest::create_command(uint8_t* cmd_data) const
{
	init_command(cmd_data);
	// The control byte is at params[2], or cmd_data[4], it is then followed by the payload, see serialize above.
	cmd_data[4] = control_code_;
}

void ControlRequest::copy_payload(uint8_t* data) const {
	std::copy(data_.begin(), data_.end(), data);
}

size_t ControlRequest::payload_size() const { 
	return data_.size();
}

std::unique_ptr<Response> ControlRequest::create_response(uint8_t source, uint8_t status, const uint8_t* data, uint16_t num) const
{
    std::unique_ptr<ControlResponse> response = std::make_unique<ControlResponse>(get_request_sequence_number(), status);
    return response;
}

#endif // SP_OVER_SLIP
