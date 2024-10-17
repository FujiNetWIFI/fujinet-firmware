#ifdef DEV_RELAY_SLIP

#include "ReadBlock.h"

ReadBlockRequest::ReadBlockRequest(const uint8_t request_sequence_number, const uint8_t device_id) : Request(request_sequence_number, CMD_READ_BLOCK, device_id), block_number_{} {}

std::vector<uint8_t> ReadBlockRequest::serialize() const
{
	std::vector<uint8_t> request_data;
	request_data.push_back(this->get_request_sequence_number());
	request_data.push_back(this->get_command_number());
	request_data.push_back(this->get_device_id());
	request_data.insert(request_data.end(), block_number_.begin(), block_number_.end());
	return request_data;
}

std::unique_ptr<Response> ReadBlockRequest::deserialize(const std::vector<uint8_t> &data) const
{
	if (data.size() != (512 + 2)) // 2 additional bytes are added to the block
	{
		throw std::runtime_error("Not enough data to deserialize ReadBlockResponse");
	}

	auto response = std::make_unique<ReadBlockResponse>(data[0], data[1]);
	if (response->get_status() == 0)
	{
		response->set_block_data(data.begin() + 2, data.end());
	}
	return response;
}

const std::array<uint8_t, 3> &ReadBlockRequest::get_block_number() const { return block_number_; }

void ReadBlockRequest::set_block_number_from_ptr(const uint8_t *ptr, const size_t offset) { std::copy_n(ptr + offset, block_number_.size(), block_number_.begin()); }

void ReadBlockRequest::set_block_number_from_bytes(uint8_t l, uint8_t m, uint8_t h)
{
	block_number_[0] = l;
	block_number_[1] = m;
	block_number_[2] = h;
}

void ReadBlockRequest::create_command(uint8_t *cmd_data) const {
	init_command(cmd_data);
	std::copy(block_number_.begin(), block_number_.end(), cmd_data + 4);
}

std::unique_ptr<Response> ReadBlockRequest::create_response(uint8_t source, uint8_t status, const uint8_t *data, uint16_t num) const {
	std::unique_ptr<ReadBlockResponse> response = std::make_unique<ReadBlockResponse>(get_request_sequence_number(), status);
	// Copy the return data if the status is OK
	if (status == 0) {
		std::vector<uint8_t> data_vector(data, data + num);
		response->set_block_data(data_vector.begin(), data_vector.end());
	}
	return response;
}


ReadBlockResponse::ReadBlockResponse(const uint8_t request_sequence_number, const uint8_t status) : Response(request_sequence_number, status), block_data_{} {}

std::vector<uint8_t> ReadBlockResponse::serialize() const
{
	std::vector<uint8_t> data;
	data.push_back(this->get_request_sequence_number());
	data.push_back(this->get_status());
	data.insert(data.end(), block_data_.begin(), block_data_.end());
	return data;
}

void ReadBlockResponse::set_block_data(std::vector<uint8_t>::const_iterator begin, std::vector<uint8_t>::const_iterator end)
{
	std::copy(begin, end, block_data_.begin()); // NOLINT(performance-unnecessary-value-param)
}

const std::array<uint8_t, 512>& ReadBlockResponse::get_block_data() const {
	return block_data_;
}

#endif
