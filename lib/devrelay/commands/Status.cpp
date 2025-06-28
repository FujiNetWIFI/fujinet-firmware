#ifdef DEV_RELAY_SLIP

#include "Status.h"

StatusRequest::StatusRequest(const uint8_t request_sequence_number, const uint8_t param_count, const uint8_t device_id, const uint8_t status_code, const uint8_t network_unit) : Request(request_sequence_number, CMD_STATUS, param_count, device_id), status_code_(status_code), network_unit_(network_unit)
{
}

std::vector<uint8_t> StatusRequest::serialize() const
{
	std::vector<uint8_t> request_data;
	request_data.push_back(this->get_request_sequence_number());
	request_data.push_back(this->get_command_number());
	request_data.push_back(this->get_param_count());
	request_data.push_back(this->get_device_id());
	request_data.resize(6);
	request_data.push_back(this->get_status_code());
	request_data.push_back(this->get_network_unit());
	request_data.resize(11);
	return request_data;
}

std::unique_ptr<Response> StatusRequest::deserialize(const std::vector<uint8_t> &data) const
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
	cmd_data[5] = network_unit_;
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


StatusResponse::StatusResponse(const uint8_t request_sequence_number, const uint8_t status) : Response(request_sequence_number, status) {}

const std::vector<uint8_t> &StatusResponse::get_data() const { return data_; }

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


#endif
