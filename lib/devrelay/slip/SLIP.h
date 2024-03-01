#pragma once

#include <cstddef>
#include <stdint.h>
#include <vector>

#define SLIP_END 0300	  /* indicates end of packet */
#define SLIP_ESC 0333	  /* indicates byte stuffing */
#define SLIP_ESC_END 0334 /* ESC ESC_END means END data byte */
#define SLIP_ESC_ESC 0335 /* ESC ESC_ESC means ESC data byte */

class SLIP
{
public:
	// these encode and decode exactly one SLIP frame, and expect it to be sane.
	static std::vector<uint8_t> encode(const std::vector<uint8_t> &data);
	static std::vector<uint8_t> decode(const std::vector<uint8_t> &data);
	static std::vector<std::vector<uint8_t>> split_into_packets(const uint8_t *data, size_t bytes_read);
};
