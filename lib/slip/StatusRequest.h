#pragma once

#include <vector>
#include <cstdint>
#include "Request.h"
#include "Response.h"

class StatusRequest : public Request
{
public:
	StatusRequest(uint8_t request_sequence_number, uint8_t sp_unit, uint8_t status_code);
	virtual std::vector<uint8_t> serialize() const override;
	std::unique_ptr<Response> deserialize(const std::vector<uint8_t>& data) const override;

	uint8_t get_status_code() const { return status_code_; }

	void create_command(uint8_t* output_data) const override;
	void copy_payload(uint8_t* data) const override {}
	size_t payload_size() const override { return 0; };
	std::unique_ptr<Response> create_response(uint8_t source, uint8_t status, const uint8_t* data, uint16_t num) const override;

private:
	uint8_t status_code_;
};
