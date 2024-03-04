#ifdef DEV_RELAY_SLIP

#include <iostream>

#include "SLIP.h"

std::vector<uint8_t> SLIP::encode(const std::vector<uint8_t> &data)
{
	std::vector<uint8_t> encoded_data;

	// start with SLIP_END
	encoded_data.push_back(SLIP_END);

	// Escape any SLIP special characters in the packet data
	for (uint8_t byte : data)
	{
		if (byte == SLIP_END || byte == SLIP_ESC)
		{
			encoded_data.push_back(SLIP_ESC);
			encoded_data.push_back(byte == SLIP_END ? SLIP_ESC_END : SLIP_ESC_ESC);
		}
		else
		{
			encoded_data.push_back(byte);
		}
	}

	// Add the SLIP END byte to the end of the encoded data
	encoded_data.push_back(SLIP_END);

	return encoded_data;
}

std::vector<uint8_t> SLIP::decode(const std::vector<uint8_t> &data)
{
	std::vector<uint8_t> decoded_data;
	auto bytes_read = data.size();

	size_t i = 0;
	while (i < bytes_read)
	{
		if (data[i] == SLIP_END)
		{
			// Start of a SLIP packet
			i++;

			// Decode the SLIP packet data
			while (i < bytes_read && data[i] != SLIP_END)
			{
				if (data[i] == SLIP_ESC)
				{
					// Escaped byte
					i++;

					if (data[i] == SLIP_ESC_END)
					{
						// Escaped END byte
						decoded_data.push_back(SLIP_END);
					}
					else if (data[i] == SLIP_ESC_ESC)
					{
						// Escaped ESC byte
						decoded_data.push_back(SLIP_ESC);
					}
					else
					{
						// Invalid escape sequence
						return std::vector<uint8_t>();
					}
				}
				else
				{
					// Non-escaped byte
					decoded_data.push_back(data[i]);
				}

				i++;
			}

			// Check for end of packet
			if (i == bytes_read || data[i] != SLIP_END)
			{
				// Incomplete SLIP packet
				return std::vector<uint8_t>();
			}
		}
		else
		{
			// Invalid SLIP packet
			return std::vector<uint8_t>();
		}

		i++;
	}

	return decoded_data;
}

// This breaks up a vector of data into a list of decoded vectors of serialized objects.
// The returned data is already "SLIP::decode"d
std::vector<std::vector<uint8_t>> SLIP::split_into_packets(const uint8_t *data, size_t bytes_read)
{
	// The list of decoded SLIP packets
	std::vector<std::vector<uint8_t>> decoded_packets;

	enum class State
	{
		NotParsing,
		Parsing
	} state = State::NotParsing;

	// Iterate over the data and find the SLIP packet boundaries
	size_t i = 0;
	const uint8_t *packet_start = nullptr; // Keep track of where the packet starts
	while (i < bytes_read)
	{
		switch (state)
		{
		case State::NotParsing:
			// If we are not yet parsing and see a SLIP_END byte (which also marks start), start parsing a new SLIP packet
			if (data[i] == SLIP_END)
			{
				state = State::Parsing;
				packet_start = data + i; // Mark the start of the packet
			}
			break;
		case State::Parsing:
			// If we see another SLIP_END byte, we have reached the end of the SLIP packet
			if (data[i] == SLIP_END)
			{
				// Extract the SLIP packet data
				std::vector<uint8_t> slip_packet_data(packet_start, data + i + 1); // Include all bytes in the packet

				// Add the data to the list of SLIP decoded packets
				decoded_packets.push_back(SLIP::decode(slip_packet_data));

				// Transition back to the NotParsing state
				state = State::NotParsing;
			}
			break;
		}
		i++;
	}

	return decoded_packets;
}


#endif
