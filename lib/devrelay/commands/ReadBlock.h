#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "../types/Request.h"
#include "../types/Response.h"

class ReadBlockRequest : public Request
{
public:
	ReadBlockRequest(uint8_t request_sequence_number, uint8_t param_count, uint8_t device_id);
	std::vector<uint8_t> serialize() const override;
	std::unique_ptr<Response> deserialize(const std::vector<uint8_t> &data) const override;
	const std::array<uint8_t, 3> &get_block_number() const;
	void set_block_number_from_ptr(const uint8_t *ptr, size_t offset);
	void set_block_number_from_bytes(uint8_t l, uint8_t m, uint8_t h);

	void create_command(uint8_t *output_data) const override;
	void copy_payload(uint8_t *data) const override {}
	size_t payload_size() const override { return 0; };
	std::unique_ptr<Response> create_response(uint8_t source, uint8_t status, const uint8_t *data, uint16_t num) const override;

private:
	std::array<uint8_t, 3> block_number_;
};


class ReadBlockResponse : public Response
{
public:
	explicit ReadBlockResponse(uint8_t request_sequence_number, uint8_t status);
	std::vector<uint8_t> serialize() const override;

	void set_block_data(std::vector<uint8_t>::const_iterator begin, std::vector<uint8_t>::const_iterator end);
	const std::array<uint8_t, 512>& get_block_data() const;

private:
	std::array<uint8_t, 512> block_data_;
};