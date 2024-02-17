#ifdef SP_OVER_SLIP

#include "WriteBlockRequest.h"
#include "WriteBlockResponse.h"

WriteBlockRequest::WriteBlockRequest(const uint8_t request_sequence_number, const uint8_t sp_unit)
	: Request(request_sequence_number, SP_WRITE_BLOCK, sp_unit), block_number_{}, block_data_{} {}

std::vector<uint8_t> WriteBlockRequest::serialize() const
{
	std::vector<uint8_t> request_data;
	request_data.push_back(this->get_request_sequence_number());
	request_data.push_back(this->get_command_number());
	request_data.push_back(this->get_sp_unit());
	request_data.insert(request_data.end(), block_number_.begin(), block_number_.end());
	request_data.insert(request_data.end(), block_data_.begin(), block_data_.end());

	return request_data;
}

std::unique_ptr<Response> WriteBlockRequest::deserialize(const std::vector<uint8_t>& data) const
{
	if (data.size() < 2)
	{
		throw std::runtime_error("Not enough data to deserialize WriteBlockResponse");
	}

	auto response = std::make_unique<WriteBlockResponse>(data[0], data[1]);
	return response;
}

const std::array<uint8_t, 3>& WriteBlockRequest::get_block_number() const
{
	return block_number_;
}

const std::array<uint8_t, 512>& WriteBlockRequest::get_block_data() const
{
	return block_data_;
}

void WriteBlockRequest::set_block_number_from_ptr(const uint8_t* ptr, const size_t offset) {
	std::copy_n(ptr + offset, block_number_.size(), block_number_.begin());
}

void WriteBlockRequest::set_block_data_from_ptr(const uint8_t* ptr, const size_t offset) {
	std::copy_n(ptr + offset, block_data_.size(), block_data_.begin());
}

void WriteBlockRequest::create_command(uint8_t* cmd_data) const
{
	init_command(cmd_data);
	std::copy(block_number_.begin(), block_number_.end(), cmd_data + 4);
}

void WriteBlockRequest::copy_payload(uint8_t* data) const {
	std::copy(block_data_.begin(), block_data_.end(), data);
}

size_t WriteBlockRequest::payload_size() const { 
	return block_data_.size();
}

std::unique_ptr<Response> WriteBlockRequest::create_response(uint8_t source, uint8_t status, const uint8_t* data, uint16_t num) const
{
  std::unique_ptr<WriteBlockResponse> response = std::make_unique<WriteBlockResponse>(get_request_sequence_number(), status);
	return response;
}

#endif // SP_OVER_SLIP
