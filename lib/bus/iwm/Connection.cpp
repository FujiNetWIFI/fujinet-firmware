#ifdef BUILD_APPLE
#ifdef SP_OVER_SLIP

#include "Connection.h"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <vector>

// This is called after AppleWin sends a request to a device, and is waiting for the response
std::vector<uint8_t> Connection::wait_for_response(uint8_t request_id, std::chrono::seconds timeout)
{
	std::unique_lock<std::mutex> lock(data_mutex_);
	// mutex is unlocked as it goes into a wait, so then the inserting thread can
	// add to map, and this can then pick it up when notified, or timeout.
	if (!data_cv_.wait_for(lock, timeout, [this, request_id]() { return data_map_.count(request_id) > 0; }))
	{
		throw std::runtime_error("Timeout waiting for response");
	}
	std::vector<uint8_t> response_data = data_map_[request_id];
	data_map_.erase(request_id);
	return response_data;
}

// This is used by devices that are waiting for requests from AppleWin.
// The codebase is used both sides of the connection.
std::vector<uint8_t> Connection::wait_for_request()
{
	// Use a timeout so we can stop waiting for responses
	while (is_connected_)
	{
		std::unique_lock<std::mutex> lock(data_mutex_);
		if (data_cv_.wait_for(lock, std::chrono::milliseconds(100), [this]() { return !data_map_.empty(); }))
		{
			const auto it = data_map_.begin();
			std::vector<uint8_t> request_data = it->second;
			data_map_.erase(it);

			return request_data;
		}
	}
	return std::vector<uint8_t>();
}

void Connection::join()
{
	if (reading_thread_.joinable())
	{
		reading_thread_.join();
	}
}

#endif
#endif