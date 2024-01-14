#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "Request.h"
#include "Response.h"

class WriteBlockRequest : public Request {
public:
  WriteBlockRequest(uint8_t request_sequence_number, uint8_t sp_unit);
  std::vector<uint8_t> serialize() const override;
  std::unique_ptr<Response>
  deserialize(const std::vector<uint8_t> &data) const override;
  const std::array<uint8_t, 3> &get_block_number() const;
  void set_block_number_from_ptr(const uint8_t *ptr, size_t offset);

  const std::array<uint8_t, 512> &get_block_data() const;
  void set_block_data_from_ptr(const uint8_t *ptr, size_t offset);
  void create_command(uint8_t *output_data) const override;
  void copy_payload(uint8_t *data) const override;
  size_t payload_size() const override;
  std::unique_ptr<Response> create_response(uint8_t source, uint8_t status,
                                            const uint8_t *data,
                                            uint16_t num) const override;

private:
  std::array<uint8_t, 3> block_number_;
  std::array<uint8_t, 512> block_data_;
};
