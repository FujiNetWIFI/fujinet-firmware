#pragma once

#include <cstdint>
#include <vector>

#include "../types/Request.h"
#include "../types/Response.h"

class ControlRequest : public Request
{
public:
	ControlRequest(const uint8_t request_sequence_number, const uint8_t param_count, const uint8_t device_id, const uint8_t control_code, const uint8_t network_unit, std::vector<uint8_t> &data);
	std::vector<uint8_t> serialize() const override;
	std::unique_ptr<Response> deserialize(const std::vector<uint8_t> &data) const override;

	const std::vector<uint8_t> &get_data() const { return data_; }
	uint8_t get_control_code() const { return control_code_; }
	uint8_t get_network_unit() const { return network_unit_; }
	void create_command(uint8_t* output_data) const override;
	void copy_payload(uint8_t* data) const override;
	size_t payload_size() const override;
	std::unique_ptr<Response> create_response(uint8_t source, uint8_t status, const uint8_t* data, uint16_t num) const override;

private:
	uint8_t control_code_;
	uint8_t network_unit_;
	std::vector<uint8_t> data_;
};

class ControlResponse : public Response
{
public:
	explicit ControlResponse(uint8_t request_sequence_number, uint8_t status);
	std::vector<uint8_t> serialize() const override;
};