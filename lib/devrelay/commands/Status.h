#pragma once

#include <cstdint>
#include <vector>

#include "../types/Request.h"
#include "../types/Response.h"

class StatusRequest : public Request
{
public:
	StatusRequest(uint8_t request_sequence_number, uint8_t param_count, uint8_t device_id, uint8_t status_code, uint8_t network_unit);
	virtual std::vector<uint8_t> serialize() const override;
	std::unique_ptr<Response> deserialize(const std::vector<uint8_t> &data) const override;

	uint8_t get_status_code() const { return status_code_; }
	uint8_t get_network_unit() const { return network_unit_; }

	void create_command(uint8_t* output_data) const override;
	void copy_payload(uint8_t* data) const override {}
	size_t payload_size() const override { return 0; };
	std::unique_ptr<Response> create_response(uint8_t source, uint8_t status, const uint8_t* data, uint16_t num) const override;

private:
	uint8_t status_code_;
	uint8_t network_unit_;
};


class StatusResponse : public Response
{
public:
	explicit StatusResponse(uint8_t request_sequence_number, uint8_t status);
	std::vector<uint8_t> serialize() const override;

	const std::vector<uint8_t> &get_data() const;
	void add_data(uint8_t d);
	void set_data(const std::vector<uint8_t>::const_iterator& begin, const std::vector<uint8_t>::const_iterator& end);

private:
	std::vector<uint8_t> data_;
};
