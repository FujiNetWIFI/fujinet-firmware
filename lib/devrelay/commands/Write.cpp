#ifdef DEV_RELAY_SLIP

#include "Write.h"

WriteRequest::WriteRequest(const uint8_t request_sequence_number, const uint8_t param_count, const uint8_t device_id) : Request(request_sequence_number, CMD_WRITE, param_count, device_id), byte_count_(), address_() {}

std::vector<uint8_t> WriteRequest::serialize() const
{
	std::vector<uint8_t> request_data;
	request_data.push_back(this->get_request_sequence_number());
	request_data.push_back(this->get_command_number());
	request_data.push_back(this->get_param_count());
	request_data.push_back(this->get_device_id());
	request_data.resize(6);
	request_data.insert(request_data.end(), get_byte_count().begin(), get_byte_count().end());
	request_data.insert(request_data.end(), get_address().begin(), get_address().end());
	request_data.resize(11);
	request_data.insert(request_data.end(), get_data().begin(), get_data().end());
	return request_data;
}

std::unique_ptr<Response> WriteRequest::deserialize(const std::vector<uint8_t> &data) const
{
	if (data.size() < 2)
	{
		throw std::runtime_error("Not enough data to deserialize WriteResponse");
	}

	auto response = std::make_unique<WriteResponse>(data[0], data[1]);
	return response;
}

const std::array<uint8_t, 2> &WriteRequest::get_byte_count() const { return byte_count_; }

const std::array<uint8_t, 3> &WriteRequest::get_address() const { return address_; }

void WriteRequest::set_byte_count_from_ptr(const uint8_t *ptr, const size_t offset) { std::copy_n(ptr + offset, byte_count_.size(), byte_count_.begin()); }

void WriteRequest::set_address_from_ptr(const uint8_t *ptr, const size_t offset) { std::copy_n(ptr + offset, address_.size(), address_.begin()); }

void WriteRequest::set_data_from_ptr(const uint8_t *ptr, const size_t offset, const size_t length)
{
	data_.clear();
	data_.insert(data_.end(), ptr + offset, ptr + offset + length);
}

void WriteRequest::create_command(uint8_t* cmd_data) const
{
	init_command(cmd_data);
	std::copy(byte_count_.begin(), byte_count_.end(), cmd_data + 4);
	std::copy(address_.begin(), address_.end(), cmd_data + 6);
}

void WriteRequest::copy_payload(uint8_t* data) const {
	std::copy(data_.begin(), data_.end(), data);
}

size_t WriteRequest::payload_size() const { 
	return data_.size();
}

std::unique_ptr<Response> WriteRequest::create_response(uint8_t source, uint8_t status, const uint8_t* data, uint16_t num) const
{
	std::unique_ptr<WriteResponse> response = std::make_unique<WriteResponse>(get_request_sequence_number(), status);
	return response;
}


WriteResponse::WriteResponse(const uint8_t request_sequence_number, const uint8_t status) : Response(request_sequence_number, status) {}

std::vector<uint8_t> WriteResponse::serialize() const
{
	std::vector<uint8_t> data;
	data.push_back(this->get_request_sequence_number());
	data.push_back(this->get_status());
	return data;
}


#endif
