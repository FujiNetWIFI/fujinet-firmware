#pragma once

#include <cstdint>
#include <vector>

#include "../types/Request.h"
#include "../types/Response.h"

class InitRequest : public Request
{
public:
	InitRequest(uint8_t request_sequence_number, uint8_t param_count, uint8_t device_id);
	std::vector<uint8_t> serialize() const override;
	std::unique_ptr<Response> deserialize(const std::vector<uint8_t> &data) const override;
	void create_command(uint8_t* output_data) const override;
	void copy_payload(uint8_t* data) const override {}
	size_t payload_size() const override { return 0; };
	std::unique_ptr<Response> create_response(uint8_t source, uint8_t status, const uint8_t* data, uint16_t num) const override;
};

class InitResponse : public Response
{
public:
	explicit InitResponse(uint8_t request_sequence_number, uint8_t status);
	std::vector<uint8_t> serialize() const override;
};