#include <chrono>
#ifdef BUILD_APPLE

#ifdef SP_OVER_SLIP

#include "Connection.h"
#include <iostream>

std::vector<uint8_t> Connection::wait_for_request()
{
    // Use a timeout so we can stop waiting for responses
    while (is_connected_) {
        std::unique_lock<std::mutex> lock(data_mutex_);
        if (data_cv_.wait_for(lock, std::chrono::milliseconds(100), [this]() { return !data_map_.empty(); })) {
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

#endif // SP_OVER_SLIP

#endif // BUILD_APPLE
